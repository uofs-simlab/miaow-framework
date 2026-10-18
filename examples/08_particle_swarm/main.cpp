/**
 * Particle swarm optimization with two job controller policies: synchronous
 * and asynchronous updates.
 *
 * Each task evaluates the Rastrigin function at one point; the controller owns all
 * optimizer state.
 *
 * Random evaluation delays make worker utilization and update timing visible.
 *
 * Synchronous updates move particles and queue their next evaluations after a full
 * generation completes; asynchronous updates do so as each particle's result arrives.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "miaow_io.hpp"

namespace {

// Rastrigin simulation with artificial evaluation costs
class RastriginEvaluation final : public miaow::SimulationUnit {
  public:
    // Delay limits are inclusive milliseconds
    explicit RastriginEvaluation(int minimum_delay_ms = 10,
                                 int maximum_delay_ms = 200)
        : minimum_delay_ms_{minimum_delay_ms},
          maximum_delay_ms_{maximum_delay_ms} {
        if (minimum_delay_ms < 0 || maximum_delay_ms < minimum_delay_ms) {
            throw std::invalid_argument(
                "Evaluation delay limits must be non-negative and min <= max");
        }
    }

    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        const std::array<double, 2> position{
            caf::get<caf::config_value::real>(task.args_[0]),
            caf::get<caf::config_value::real>(task.args_[1])};

        // Seed from both coordinates' full bit patterns for repeatable costs at each point
        const auto bits = std::bit_cast<std::array<std::uint32_t, 4>>(position);
        std::seed_seq seed{bits.begin(), bits.end()};
        std::mt19937 random{seed};
        std::uniform_int_distribution<int> delay_ms{minimum_delay_ms_, maximum_delay_ms_};
        const int delay = delay_ms(random);

        // Sleeping simulates uneven evaluation costs and occupies a CAF scheduler thread
        std::this_thread::sleep_for(std::chrono::milliseconds{delay});

        // Evaluate the Rastrigin function for the given task arguments
        double value = 0.0;
        for (const double x : position) {
            value += x * x + 10.0 * (1.0 - std::cos(2.0 * std::numbers::pi * x));
        }

        // Return the objective through TaskResult::output_ for the controller to consume
        return miaow::TaskResult{miaow::TaskStatus::succeeded, {},
                                 caf::config_value{value}};
    }

  private:
    int minimum_delay_ms_;
    int maximum_delay_ms_;
};

struct Particle {
    // Latest proposed position, retained until its result is consumed
    std::array<double, 2> position;

    // Displacement per optimizer update, rather than velocity per second of runtime
    std::array<double, 2> velocity;

    // Position with the lowest objective value observed for this particle
    std::array<double, 2> best_position;

    // Infinity ensures the first finite objective value establishes a personal best
    double best_value = std::numeric_limits<double>::infinity();

    // Per-particle evaluation index: zero for initialization, then one per proposed move
    std::size_t evaluation = 0;
};

// Job-owned optimizer state; controller callbacks are serialized by the runtime
class ParticleSwarmController final : public miaow::JobController {
  public:
    ParticleSwarmController(
        miaow::SimulationUnitId objective,
        miaow::TaskGenerationPolicy policy,
        std::pair<double, double> bounds = {-5.12, 5.12},
        std::size_t swarm_size = 32,
        std::chrono::duration<double> time_limit = std::chrono::seconds{30},
        std::size_t evaluation_budget = 100000,
        double cognitive_weight = 2.05,
        double social_weight = 2.05,
        std::uint32_t initial_seed = 42,
        std::uint32_t movement_seed = 1729)
        : JobController{policy},
          objective_{objective},
          bounds_{bounds},
          time_limit_{time_limit},
          evaluation_budget_{evaluation_budget},
          cognitive_weight_{cognitive_weight},
          social_weight_{social_weight},
          movement_seed_{movement_seed},
          swarm_(swarm_size) {
        if (swarm_size == 0 || evaluation_budget == 0 ||
            !std::isfinite(time_limit.count()) ||
            time_limit <= std::chrono::duration<double>::zero()) {
            throw std::invalid_argument(
                "Swarm size, evaluation budget, and finite time limit must be positive");
        }
        if (!std::isfinite(bounds_.first) || !std::isfinite(bounds_.second) ||
            bounds_.first >= bounds_.second) {
            throw std::invalid_argument("Search bounds must be finite and increasing");
        }

        // Clerc and Kennedy (2002) constriction with kappa = 1 requires phi > 4
        const double phi = cognitive_weight_ + social_weight_;
        if (!std::isfinite(phi) || phi <= 4.0) {
            throw std::invalid_argument(
                "Cognitive and social weights must have a finite sum greater than 4");
        }
        constriction_factor_ = 2.0 / std::abs(2.0 - phi - std::sqrt(phi * phi - 4.0 * phi));

        // Recreate the same initial swarm for both policies. position and velocity are
        // sampling distributions, while random supplies their shared initial-state stream.
        std::mt19937 random{initial_seed};
        std::uniform_real_distribution<double> position{bounds_.first,
                                                       bounds_.second};

        // Example initial speeds span +/-10% of the search width per coordinate
        const double initial_speed_limit = 0.1 * (bounds_.second - bounds_.first);
        std::uniform_real_distribution<double> velocity{-initial_speed_limit,
                                                        initial_speed_limit};

        // Draw both coordinates of every particle's position and velocity
        for (Particle& particle : swarm_) {
            for (std::size_t dimension = 0; dimension < 2; ++dimension) {
                particle.position[dimension] = position(random);
                particle.velocity[dimension] = velocity(random);
            }

            // No objective has been evaluated yet, so best_value remains infinity
            particle.best_position = particle.position;

            // Retain the starting position for animation playback
            initial_positions_.push_back(particle.position);
        }
    }

    std::vector<miaow::Task> generate_initial_tasks() override {
        start_time_ = std::chrono::steady_clock::now();

        // One task per particle forms the first batch. The runtime queues tasks beyond
        // the worker limit and dispatches them as workers become available.
        std::vector<miaow::Task> tasks;
        for (std::size_t id = 0; id < swarm_.size(); ++id) {
            if (submitted_ == evaluation_budget_ ||
                std::chrono::steady_clock::now() - start_time_ >= time_limit_) {
                break;
            }
            const Particle& particle = swarm_[id];

            // Pass the point; metadata identifies the particle for result handling
            tasks.emplace_back(
                objective_,
                std::vector<caf::config_value>{caf::config_value{particle.position[0]},
                                               caf::config_value{particle.position[1]}},
                miaow::TaskMetadata{{"particle", std::to_string(id)}});
            ++submitted_;
        }
        return tasks;
    }

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        // Get the id of each particle that has completed evaluation since last callback
        std::vector<std::size_t> particles_to_update;
        for (const miaow::TaskRecord& record : new_records) {
            // A failed evaluation cannot update the optimizer; propagate it as a job error
            if (record.result_.status_ != miaow::TaskStatus::succeeded) {
                throw std::runtime_error("Particle evaluation failed: "
                                         + record.result_.message_);
            }

            // Metadata maps the runtime record back to its zero-based particle ID
            const unsigned long id = std::stoul(record.metadata_.at("particle"));
            Particle& particle = swarm_[id];

            // Only one evaluation per particle is outstanding, so its stored position
            // still matches this value. Retain the personal best when the result improves.
            const double value = caf::get<caf::config_value::real>(record.result_.output_);
            if (value < particle.best_value) {
                particle.best_value = value;
                particle.best_position = particle.position;
            }
            particles_to_update.push_back(id);
        }

        // Get the global best particle from the swarm
        const Particle& best = *std::min_element(
            swarm_.begin(), swarm_.end(), [](const Particle& a, const Particle& b) {
                return a.best_value < b.best_value;
            });
        const std::array<double, 2> global_best = best.best_position;

        // Propose tasks in particle-ID order, independent of this callback's result order
        std::sort(particles_to_update.begin(), particles_to_update.end());

        // Add new tasks for particles that need to be updated
        std::vector<miaow::Task> tasks;
        for (const std::size_t id : particles_to_update) {
            // Check both search limits before moving the particle
            if (submitted_ == evaluation_budget_ ||
                std::chrono::steady_clock::now() - start_time_ >= time_limit_) {
                break;
            }

            // Move the particle according to the PSO update rule
            update_particle(id, global_best);

            // Add a new task to the queue for evaluating the particle's new position
            tasks.emplace_back(
                objective_,
                std::vector<caf::config_value>{caf::config_value{swarm_[id].position[0]},
                                               caf::config_value{swarm_[id].position[1]}},
                miaow::TaskMetadata{{"particle", std::to_string(id)}});
            ++submitted_;
        }

        // Save current particle positions for animation playback
        std::vector<std::array<double, 2>> positions;
        for (const Particle& particle : swarm_) {
            positions.push_back(particle.position);
        }

        // Timestamp the snapshot at the triggering result's completion
        const miaow::TaskRecord& last = new_records.back();
        const double time =
            (*last.dispatch_offset_from_job_start_ + last.run_time_).count();

        // Record the progress snapshot for animation playback
        updates_.emplace_back(caf::config_value::dictionary{
            {"time", caf::config_value{time}},
            {"positions", caf::config_value{std::move(positions)}},
            {"best_position", caf::config_value{global_best}}});

        // With no new tasks, the runtime finishes once queued and active evaluations drain
        return tasks;
    }

    // Transfer the buffered history to the runtime for job_record.json and the caller
    caf::config_value finalize() override {
        // Package the labels, plotting limits, and swarm history used by animate.py
        caf::config_value::dictionary output;
        output.emplace("policy", task_generation_policy()
                                     == miaow::TaskGenerationPolicy::batch_synchronous
                                 ? "synchronous" : "asynchronous");
        output.emplace("bounds", std::array{bounds_.first, bounds_.second});
        output.emplace("wall_time_limit_seconds", time_limit_.count());
        output.emplace("initial_positions", std::move(initial_positions_));
        output.emplace("updates", std::move(updates_));
        return caf::config_value{std::move(output)};
    }

  private:
    // Apply the constriction PSO rule to one particle and clamp it to the search bounds
    void update_particle(std::size_t id, const std::array<double, 2>& global_best) {
        Particle& particle = swarm_[id];

        // Match movement draws between policies by seed, particle ID, and evaluation index
        std::seed_seq key{movement_seed_, static_cast<std::uint32_t>(id),
                          static_cast<std::uint32_t>(++particle.evaluation)};
        std::mt19937 random{key};
        std::uniform_real_distribution<double> uniform{0.0, 1.0};

        // Update each position and velocity component of the particle
        for (std::size_t dimension = 0; dimension < 2; ++dimension) {
            // x and v refer to the coordinate and displacement being updated
            double& x = particle.position[dimension];
            double& v = particle.velocity[dimension];

            // r1 weights the personal-best pull; r2 weights the global-best pull
            const double r1 = uniform(random);
            const double r2 = uniform(random);

            // update velocity
            v = constriction_factor_ * (v + cognitive_weight_ * r1
                * (particle.best_position[dimension] - x)
                   + social_weight_ * r2 * (global_best[dimension] - x));

            // Clamp the updated position and clear outward velocity when a bound is crossed
            x += v;
            if (x < bounds_.first) {
                x = bounds_.first;
                v = 0.0;
            } else if (x > bounds_.second) {
                x = bounds_.second;
                v = 0.0;
            }
        }
    }

    // Registry ID used by every objective-evaluation task
    miaow::SimulationUnitId objective_;

    // Per-controller settings and their derived Clerc-Kennedy constriction
    std::pair<double, double> bounds_;
    std::chrono::duration<double> time_limit_;
    std::size_t evaluation_budget_;
    double cognitive_weight_;
    double social_weight_;
    std::uint32_t movement_seed_;
    double constriction_factor_;

    // Search clock and submitted count include initialization and queued work
    std::chrono::steady_clock::time_point start_time_;
    std::size_t submitted_ = 0;

    // Persistent position, velocity, and personal-best state indexed by particle ID
    std::vector<Particle> swarm_;

    // Initial positions and per-update snapshots for animation playback
    std::vector<std::array<double, 2>> initial_positions_;
    caf::config_value::list updates_;
};

} // namespace

int main(int argc, char* argv[]) {
    // Keep relative output paths rooted in this example's source directory
    if (!miaow::ensure_expected_run_dir(MIAOW_PARTICLE_SWARM_RUN_DIR)) {
        return 1;
    }
    try {
        // Default to eight workers, independently of the swarm size.
        // CLI options can override the worker limit and the default output directory.
        miaow::RuntimeOptions options{8};
        options.output_root_ = "output";
        miaow::parse_runtime_cli_options(argc, argv, options);

        // Register the Rastrigin evaluator with its default artificial evaluation costs
        miaow::SimulationUnitRegistry registry;
        const miaow::SimulationUnitId objective = registry.add<RastriginEvaluation>();

        // Create the runtime with the registered simulation units and the parsed options
        miaow::Runtime runtime{registry, options};

        // Sequential jobs share a runtime but restart the same initial swarm and seeds.
        // policy controls callback timing; name supplies the human-readable result label.
        for (const auto& [policy, name] : {
                 std::pair{miaow::TaskGenerationPolicy::batch_synchronous, "synchronous"},
                 std::pair{miaow::TaskGenerationPolicy::asynchronous, "asynchronous"}}) {
            // Create independent optimizer state; runtime initializes it at job start
            std::unique_ptr<ParticleSwarmController> controller =
                std::make_unique<ParticleSwarmController>(objective, policy);

            // Transfer ownership to the runtime and wait for the final job record
            const miaow::JobRecord job =
                runtime.submit_job(std::move(controller)).get();

            // Report task failures before summarizing convergence
            if (!miaow::all_tasks_succeeded(job)) {
                miaow::print_job_summary(job);
                return 1;
            }

            // Summarize convergence directly from the completed task results
            std::cout << name << ": " << job.tasks_.size()
                      << " evaluations in " << job.run_time_.count() << " s";
            if (!job.tasks_.empty()) {
                const auto best = std::min_element(
                    job.tasks_.begin(), job.tasks_.end(),
                    [](const miaow::TaskRecord& a, const miaow::TaskRecord& b) {
                        return caf::get<caf::config_value::real>(a.result_.output_)
                             < caf::get<caf::config_value::real>(b.result_.output_);
                    });
                std::cout << "; best = "
                          << caf::get<caf::config_value::real>(best->result_.output_);
            }
            std::cout << '\n';

            // Show the run directory containing both job records for animation playback
            if (!options.output_root_.empty()) {
                std::cout << "  " << options.output_root_ << "/run_" << job.run_id_ << '\n';
            }
        }
        return 0;
    } catch (const std::exception& exception) {
        // Runtime, controller, and argument errors reach the command line as failures
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
