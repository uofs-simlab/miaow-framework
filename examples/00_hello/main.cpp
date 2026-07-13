/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit
 * and runtime. This example defines a HelloSimulationUnit that echoes string task inputs
 * and uses the Runtime API to execute two async jobs, each with two tasks, that simply
 * pass string messages through the simulation unit and print the results and diagnostics.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs

/**
 * @brief A simple simulation unit that returns a string task argument.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
  public:
    /**
     * @brief Override execute to return the first task string argument.
     * In a real simulation unit, this is where you would implement the logic to initialize
     * and run your model.
     * @param task Task payload; args[0] is expected to be a string.
     * @return Structured task result with status and message.
     */
    miaow::TaskResult execute(const miaow::Task& task,
                              miaow::ExecutionContext& /*context*/) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }
};

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    // Initialize runtime options and parse runtime CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId hello = registry.add<HelloSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    // Create two jobs with two tasks each
    std::vector<miaow::Task> first_job_tasks{
        miaow::Task{hello, "hello"},
        miaow::Task{hello, "cats"},
    };
    std::vector<miaow::Task> second_job_tasks{
        miaow::Task{hello, "hello again"},
        miaow::Task{hello, "more cats"},
    };

    // Submit tasks as two separate jobs and wait for results
    miaow::JobHandle first_job = runtime.submit_job(first_job_tasks);
    miaow::JobHandle second_job = runtime.submit_job(second_job_tasks);
    const miaow::JobRecord first_job_record = first_job.get();
    const miaow::JobRecord second_job_record = second_job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(first_job_record);
    miaow::print_task_results(second_job_record);

    // Print timing diagnostics for each job run
    miaow::print_diagnostics(first_job_record);
    miaow::print_diagnostics(second_job_record);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(first_job_record) &&
                   miaow::all_tasks_succeeded(second_job_record)
               ? 0
               : 1;
}
