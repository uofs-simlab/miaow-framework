#pragma once

/**
 * Heron's method example using one job-owned controller.
 *
 * The workflow approximates the positive square root of a positive radicand S using
 *
 *     x_(n+1) = 1/2 * (x_n + S / x_n)
 *
 * The executor retains the current estimate across tasks. Each task requests one
 * iteration and returns the updated estimate and residual. The controller requests
 * iterations until the residual meets its tolerance or the iteration limit is reached.
 */

#include <caf/settings.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "miaow.hpp" // Core MIAOW runtime API
#include <nlohmann/json.hpp>

namespace miaow::examples::controller {

constexpr double default_radicand = 2.0;
constexpr double default_initial_estimate = 1.0;
constexpr double default_residual_tolerance = 1e-5;
constexpr std::size_t default_maximum_iterations = 20;

/**
 * @brief Perform one Heron iteration using the unit's current estimate.
 */
class HeronIterationTaskExecutor final : public miaow::TaskExecutor {
  public:
    HeronIterationTaskExecutor(double radicand, double initial_estimate)
        : radicand_{radicand}, estimate_{initial_estimate} {}

    caf::result<miaow::TaskResult> execute(const miaow::Task&) override {
        // Perform one iteration of Heron's method: x_(n+1) = 1/2 * (x_n + S / x_n)
        estimate_ = 0.5 * (estimate_ + radicand_ / estimate_);
        const double residual = std::abs(estimate_ * estimate_ - radicand_);

        caf::config_value::dictionary output{{"estimate", caf::config_value{estimate_}},
                                             {"residual", caf::config_value{residual}}};
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "Heron iteration complete",
                                 caf::config_value{std::move(output)}};
    }

  private:
    double radicand_;
    double estimate_;
};

/**
 * @brief Generate iterations until the residual meets a tolerance.
 */
class IterationJobController final : public miaow::JobController {
  public:
    IterationJobController(miaow::TaskExecutorId task_executor_id,
                           double residual_tolerance, std::size_t maximum_iterations)
        : task_executor_id_{task_executor_id}, residual_tolerance_{residual_tolerance},
          maximum_iterations_{maximum_iterations} {}

    void initialize(miaow::Job& job) override {
        unit_id_ = job.add_unit("iteration", task_executor_id_);
        job.add_task(unit_id_, "iterate");
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        // Fail the job if a unit becomes unavailable
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        iterations_ += completed_task_ids.size();

        const miaow::TaskResult& result = *job.result(completed_task_ids.back());

        // If the last task failed, stop generating new tasks
        if (result.status_ != miaow::TaskStatus::succeeded) {
            return;
        }

        // Get the output dictionary from the last completed task's result
        const caf::config_value::dictionary* output =
            caf::get_if<caf::config_value::dictionary>(&result.output_);
        if (output == nullptr) {
            throw std::runtime_error("JobController output error: expected a dictionary");
        }

        // Use the residual to decide whether the unit needs another iteration
        const caf::config_value::real* residual =
            caf::get_if<caf::config_value::real>(output, "residual");
        if (residual == nullptr) {
            throw std::runtime_error("JobController output error: residual must be a real");
        }

        if (*residual <= residual_tolerance_) {
            return;
        }

        // Fail the job if the maximum iteration count has been reached
        if (iterations_ >= maximum_iterations_) {
            job.fail("Convergence error: maximum iteration count reached");
            return;
        }

        // Otherwise, request another iteration using the unit's retained estimate
        job.add_task(unit_id_, "iterate");
    }

  private:
    miaow::TaskExecutorId task_executor_id_;
    miaow::SimulationUnitId unit_id_{0};
    double residual_tolerance_;
    std::size_t maximum_iterations_;
    std::size_t iterations_ = 0;
};

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry,
                           const nlohmann::json& config) {
    const double radicand = config.value("radicand", default_radicand);
    const double initial_estimate =
        config.value("initial_estimate", default_initial_estimate);
    const double residual_tolerance =
        config.value("residual_tolerance", default_residual_tolerance);
    const std::size_t maximum_iterations =
        config.value("maximum_iterations", default_maximum_iterations);

    // Initialize each unit's numerical state through its executor constructor
    const miaow::TaskExecutorId heron_iteration =
        registry.add<HeronIterationTaskExecutor>(radicand, initial_estimate);

    // Runtime initializes the controller before dispatching its first task
    miaow::Job job;
    job.set_controller(std::make_unique<IterationJobController>(heron_iteration,
                                                                residual_tolerance,
                                                                maximum_iterations));

    return job;
}

} // namespace miaow::examples::controller
