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
#include "../config.hpp"

namespace {

constexpr double default_radicand = 2.0;
constexpr double default_initial_estimate = 1.0;
constexpr double default_residual_tolerance = 1e-5;
constexpr std::size_t default_maximum_iterations = 20;

/**
 * @brief Perform one Heron iteration and output the next estimate and residual.
 */
class HeronIterationTaskExecutor final : public miaow::TaskExecutor {
  public:
    explicit HeronIterationTaskExecutor(double radicand) : radicand_{radicand} {}

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
    IterationJobController(miaow::TaskExecutorId task_executor_id,
                           double residual_tolerance, std::size_t maximum_iterations,
                           double initial_estimate)
        : task_executor_id_{task_executor_id}, residual_tolerance_{residual_tolerance},
          maximum_iterations_{maximum_iterations}, initial_estimate_{initial_estimate} {}

    void initialize(miaow::Job& job) override {
        unit_ = job.add_unit("iteration", task_executor_id_);
        job.add_task(unit_, "iterate", initial_estimate_);
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskRecord> new_records,
                  std::span<const miaow::UnitFailure> failed_units) override {
        // Fail the job if a unit becomes unavailable
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        iterations_ += new_records.size();

        // Get the last completed task record
        const miaow::TaskRecord& completed_task = new_records.back();

        // If the last task failed, stop generating new tasks
        if (completed_task.result_.status_ != miaow::TaskStatus::succeeded) {
            return;
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
            return;
        }

        // Fail the job if the maximum iteration count has been reached
        if (iterations_ >= maximum_iterations_) {
            job.fail("Convergence error: maximum iteration count reached");
            return;
        }

        // Otherwise, generate a new task with the next estimate
        job.add_task(unit_, "iterate", *estimate);
    }

  private:
    miaow::TaskExecutorId task_executor_id_;
    miaow::SimulationUnitHandle unit_;
    double residual_tolerance_;
    std::size_t maximum_iterations_;
    double initial_estimate_;
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
        // Resolve example inputs and defaults before constructing execution objects
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);
        const double radicand = config.value("radicand", default_radicand);
        const double initial_estimate =
            config.value("initial_estimate", default_initial_estimate);
        const double residual_tolerance =
            config.value("residual_tolerance", default_residual_tolerance);
        const std::size_t maximum_iterations =
            config.value("maximum_iterations", default_maximum_iterations);

        miaow::TaskExecutorRegistry registry;

        // Register the Heron iteration task executor with the radicand
        const miaow::TaskExecutorId heron_iteration =
            registry.add<HeronIterationTaskExecutor>(radicand);
        miaow::Runtime runtime(registry, runtime_options);

        // Create a job controller that generates new tasks until convergence
        std::unique_ptr<miaow::JobController> controller =
            std::make_unique<IterationJobController>(heron_iteration, residual_tolerance,
                                                     maximum_iterations, initial_estimate);

        // Runtime initializes the controller before dispatching its first task
        miaow::Job job;
        job.set_controller(std::move(controller));

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord job_record = job_handle.get();

        // Report task failures before displaying the converged iteration sequence
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
