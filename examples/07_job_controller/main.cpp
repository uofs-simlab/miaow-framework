/**
 * Heron's method example using one job-owned controller.
 *
 * The workflow approximates the positive square root of a positive radicand S using
 *
 *     x_(n+1) = 1/2 * (x_n + S / x_n)
 *
 * Each task supplies the current estimate, performs one iteration, and returns a
 * TaskResult::output_ dictionary containing the next estimate and its residual.
 * IterationJobController uses the returned estimate as the next task's input and stops
 * generating tasks when the residual reaches its configured tolerance. The maximum
 * iteration count reports non-convergence instead of allowing the job to continue
 * indefinitely. This run uses S = 2 and x_0 = 1 to find sqrt(2) with a residual tolerance
 * of 1e-5 and a maximum of 20 iterations.
 */

#include <caf/settings.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs

namespace {

constexpr double radicand = 2.0;
constexpr double initial_estimate = 1.0;
constexpr double residual_tolerance = 1e-5;
constexpr std::size_t maximum_iterations = 20;

/**
 * @brief Perform one Heron iteration and output the next estimate and residual.
 */
class HeronIterationSimulationUnit final : public miaow::SimulationUnit {
  public:
    explicit HeronIterationSimulationUnit(double radicand) : radicand_{radicand} {}

    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        // Get the current estimate from the task arguments
        const caf::config_value::real* estimate =
            task.args_.size() == 1
                ? caf::get_if<caf::config_value::real>(&task.args_.front())
                : nullptr;
        if (estimate == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed,
                                     "Heron input error: expected one real estimate"};
        }

        // Perform one iteration of Heron's method: x_(n+1) = 1/2 * (x_n + S / x_n)
        const double next_estimate = 0.5 * (*estimate + radicand_ / *estimate);
        const double residual = std::abs(next_estimate * next_estimate - radicand_);

        // Return the next estimate and residual to the job controller in a dictionary
        caf::config_value::dictionary output{{"estimate", caf::config_value{next_estimate}},
                                             {"residual", caf::config_value{residual}}};
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "Heron iteration complete",
                                 caf::config_value{std::move(output)}};
    }

  private:
    double radicand_;
};

/**
 * @brief Generate iterations until the residual meets a tolerance.
 */
class IterationJobController final : public miaow::JobController {
  public:
    IterationJobController(miaow::SimulationUnitId simulation_unit_id,
                           double residual_tolerance, std::size_t maximum_iterations)
        : simulation_unit_id_{simulation_unit_id}, residual_tolerance_{residual_tolerance},
          maximum_iterations_{maximum_iterations} {}

    std::vector<miaow::Task> generate_initial_tasks() override {
        return {miaow::Task{simulation_unit_id_, caf::config_value{initial_estimate}}};
    }

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        iterations_ += new_records.size();

        // Get the last completed task record
        const miaow::TaskRecord& completed_task = new_records.back();

        // If the last task failed, stop generating new tasks
        if (completed_task.result_.status_ != miaow::TaskStatus::succeeded) {
            return {};
        }

        // Get the output dictionary from the last completed task's result
        const caf::config_value::dictionary* output =
            caf::get_if<caf::config_value::dictionary>(&completed_task.result_.output_);
        if (output == nullptr) {
            throw std::runtime_error("JobController output error: expected a dictionary");
        }

        // Extract the estimate and residual from the output dictionary
        const caf::config_value::real* estimate =
            caf::get_if<caf::config_value::real>(output, "estimate");
        const caf::config_value::real* residual =
            caf::get_if<caf::config_value::real>(output, "residual");
        if (estimate == nullptr || residual == nullptr) {
            throw std::runtime_error(
                "JobController output error: estimate and residual must be reals");
        }

        // If the residual is below the tolerance, stop generating new tasks
        if (*residual <= residual_tolerance_) {
            return {};
        }

        // If the maximum iteration count has been reached, throw an error
        if (iterations_ >= maximum_iterations_) {
            throw std::runtime_error("Convergence error: maximum iteration count reached");
        }

        // Otherwise, generate a new task with the next estimate
        return {miaow::Task{simulation_unit_id_, caf::config_value{*estimate}}};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    double residual_tolerance_;
    std::size_t maximum_iterations_;
    std::size_t iterations_ = 0;
};

} // namespace

int main(int argc, char* argv[]) {
    // Require execution from the example source directory so relative input paths resolve
    if (!miaow::ensure_expected_run_dir(MIAOW_JOB_CONTROLLER_RUN_DIR)) {
        return 1;
    }

    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

        miaow::SimulationUnitRegistry registry;

        // Register the Heron iteration simulation unit with the radicand
        const miaow::SimulationUnitId heron_iteration =
            registry.add<HeronIterationSimulationUnit>(radicand);
        miaow::Runtime runtime(registry, runtime_options);

        // Create a job controller that generates new tasks until convergence
        std::unique_ptr<miaow::JobController> controller =
            std::make_unique<IterationJobController>(heron_iteration, residual_tolerance,
                                                     maximum_iterations);

        // Runtime asks the controller for its first task when the job starts
        miaow::JobHandle job = runtime.submit_job(std::move(controller));

        // Get the final job record and check if all tasks succeeded
        const miaow::JobRecord job_record = job.get();
        miaow::print_job_summary(job_record);
        if (!miaow::all_tasks_succeeded(job_record)) {
            return 1;
        }

        // Print the structured output recorded for every completed iteration
        std::cout << "\nHeron iterations:\n" << std::setprecision(10);
        for (const miaow::TaskRecord& task_record : job_record.tasks_) {
            const caf::config_value::dictionary& output =
                caf::get<caf::config_value::dictionary>(task_record.result_.output_);
            std::cout << "  iteration " << task_record.task_id_ + 1 << ": estimate="
                      << caf::get<caf::config_value::real>(output, "estimate")
                      << ", residual="
                      << caf::get<caf::config_value::real>(output, "residual") << "\n";
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
