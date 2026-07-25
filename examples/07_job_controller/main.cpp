/**
 * Dynamic task generation example using one job-owned controller.
 */

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs

namespace {

// Start two tasks so the default two-worker run begins with both workers busy
constexpr std::size_t initial_task_count = 2;

// Limit the complete job to two initial tasks and three generated tasks
constexpr std::size_t total_task_budget = 5;

// Record the task ID and completed-record count observed when the task is created
std::string hello_message(std::size_t task_id, std::size_t finished_task_count) {
    return "hello from task " + std::to_string(task_id) +
           "; finished tasks: " + std::to_string(finished_task_count);
}

/**
 * @brief Echo one task message after a randomized execution delay.
 */
class HelloSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task,
                              miaow::ExecutionContext& /*context*/) override {
        // Each task stores its reporting message as the first task argument
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing hello message"};
        }

        // Give each execution thread an independent random sequence
        thread_local std::mt19937 random_generator{std::random_device{}()};

        // Make parallel task completion order nondeterministic but keep the test bounded
        std::uniform_int_distribution<int> sleep_duration_distribution{50, 200};
        const int sleep_duration_ms = sleep_duration_distribution(random_generator);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration_ms));

        // Include the sampled duration without making tests depend on its value
        return miaow::TaskResult{miaow::TaskStatus::succeeded,
                                 *message + "; slept " + std::to_string(sleep_duration_ms) +
                                     " ms"};
    }
};

/**
 * @brief Generate one replacement hello task after each completion.
 */
class HelloJobController final : public miaow::JobController {
  public:
    HelloJobController(miaow::SimulationUnitId hello_simulation_unit_id,
                       std::size_t task_budget, std::size_t next_task_id)
        : hello_simulation_unit_id_{hello_simulation_unit_id}, task_budget_{task_budget},
          next_task_id_{next_task_id} {}

    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& completed_task_records) override {
        // If the task budget is reached, do not generate any more tasks
        if (next_task_id_ >= task_budget_) {
            return {};
        }

        // One completion creates one task whose message reports its ID and finished count
        const std::size_t task_id = next_task_id_++;
        return {miaow::Task{hello_simulation_unit_id_,
                            hello_message(task_id, completed_task_records.size())}};
    }

  private:
    // SimulationUnitId copied into every generated task
    miaow::SimulationUnitId hello_simulation_unit_id_;

    // Maximum number of initial and generated tasks in this job
    std::size_t task_budget_;

    // Consecutive ID to include in the next generated task message
    std::size_t next_task_id_;
};

} // namespace

int main(int argc, char* argv[]) {
    // Require execution from the example source directory so relative input paths resolve
    if (!miaow::ensure_expected_run_dir(MIAOW_JOB_CONTROLLER_RUN_DIR)) {
        return 1;
    }

    // Use two workers by default while allowing the standard CLI override
    miaow::RuntimeOptions runtime_options{2};
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

    // Create two initial tasks that each print a message and the number of finished tasks
    std::vector<miaow::Task> initial_tasks{
        miaow::Task{hello, hello_message(0, 0)},
        miaow::Task{hello, hello_message(1, 0)},
    };

    // Create the controller with task ID 2 as its first generated task ID
    std::unique_ptr<miaow::JobController> controller =
        std::make_unique<HelloJobController>(hello, total_task_budget, initial_task_count);

    // Transfer the initial tasks and controller ownership to one submitted job
    const miaow::JobRecord job_record =
        runtime.submit_job(std::move(initial_tasks), std::move(controller)).get();

    // Verify that five task records exist, including two initial and three generated tasks
    if (job_record.tasks_.size() != total_task_budget) {
        std::cerr << "expected " << total_task_budget << " task records\n";
        return 1;
    }

    // Verify consecutive task IDs, runtime-assigned names, and successful results
    for (std::size_t task_index = 0; task_index < job_record.tasks_.size(); ++task_index) {
        const miaow::TaskRecord& task_record = job_record.tasks_.at(task_index);
        if (task_record.task_id_ != static_cast<miaow::TaskId>(task_index) ||
            task_record.task_name_ != "task_" + std::to_string(task_index) ||
            task_record.result_.status_ != miaow::TaskStatus::succeeded) {
            std::cerr << "unexpected record for task " << task_index << "\n";
            return 1;
        }
    }

    // Print task results and timing diagnostics
    miaow::print_task_results(job_record);
    miaow::print_diagnostics(job_record);
    return 0;
}
