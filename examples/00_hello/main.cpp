/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit
 * and runtime. This example defines a HelloSimulationUnit that echoes string task inputs
 * and uses the Runtime API to execute two async jobs, each with two tasks, that simply
 * pass string messages through the simulation unit and print the results and diagnostics.
*/

#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_cli.hpp" // CLI parser for example programs

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
    miaow::TaskResult execute(const miaow::Task& task) override {
        miaow::TaskResult task_result;
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            task_result.status_ = miaow::TaskStatus::failed;
            task_result.message_ = "missing message";
            return task_result;
        }
        task_result.status_ = miaow::TaskStatus::succeeded;
        task_result.message_ = *message;
        return task_result;
    }
};

int main(int argc, char* argv[]) {
    // Initialize runtime options and parse runtime-wide CLI arguments
    miaow::RuntimeOptions runtime_options;
    const std::string input_error =
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    if (!input_error.empty()) {
        std::cerr << input_error << "\n";
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
    const miaow::JobResult first_job_result = first_job.get();
    const miaow::JobResult second_job_result = second_job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(first_job_result);
    miaow::print_task_results(second_job_result);

    // Print timing diagnostics for each job run
    miaow::print_diagnostics(first_job_result.diagnostics_, 0);
    miaow::print_diagnostics(second_job_result.diagnostics_, 1);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(first_job_result) &&
            miaow::all_tasks_succeeded(second_job_result) ? 0 : 1;
}
