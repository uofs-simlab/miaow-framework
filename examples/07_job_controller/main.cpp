/**
 * Dynamic task generation example using one job-owned controller.
 */

#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs

namespace {

// Limit the workflow to one initial task and two generated tasks
constexpr std::size_t total_task_count = 3;

/**
 * @brief Echo one task message.
 */
class HelloSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing hello message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }
};

/**
 * @brief Generate one replacement hello task after each completion.
 */
class HelloJobController final : public miaow::JobController {
  public:
    HelloJobController(miaow::SimulationUnitId hello_simulation_unit_id,
                       std::size_t task_count)
        : hello_simulation_unit_id_{hello_simulation_unit_id}, task_count_{task_count} {}

    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& completed_task_records) override {
        // Stop after the requested number of task records has completed
        if (completed_task_records.size() >= task_count_) {
            return {};
        }

        // Each completion creates one replacement task
        return {miaow::Task{hello_simulation_unit_id_,
                            "generated after " +
                                std::to_string(completed_task_records.size()) +
                                " completed task(s)"}};
    }

  private:
    // SimulationUnitId copied into every generated task
    miaow::SimulationUnitId hello_simulation_unit_id_;

    // Maximum number of initial and generated tasks in this job
    std::size_t task_count_;
};

} // namespace

int main(int argc, char* argv[]) {
    // Require execution from the example source directory so relative input paths resolve
    if (!miaow::ensure_expected_run_dir(MIAOW_JOB_CONTROLLER_RUN_DIR)) {
        return 1;
    }

    // Parse the standard runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register the hello simulation unit and construct the runtime
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId hello = registry.add<HelloSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    // Create a controller that generates one task after each completion
    std::unique_ptr<miaow::JobController> controller =
        std::make_unique<HelloJobController>(hello, total_task_count);

    // Submit one initial task and transfer controller ownership to the runtime
    miaow::JobHandle job = runtime.submit_job(
        std::vector<miaow::Task>{miaow::Task{hello, "initial task"}},
        std::move(controller));
    const miaow::JobRecord job_record = job.get();

    // Print the initial and generated task results with aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
