/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit
 * and runtime. This example defines a HelloSimulationUnit that echoes string task inputs
 * and uses the Runtime API to execute two async jobs, each with two tasks, that simply
 * pass string messages through the simulation unit and print the results and diagnostics.
*/

#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "miaow.hpp" // Core MIAOW runtime API

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
     * @return The first string argument, or an error message if missing/invalid.
     */
    std::string execute(const miaow::Task& task) override {
        if (task.args_.empty()) {
            return "missing task string";
        }
        const std::string* message = caf::get_if<std::string>(&task.args_.front());
        if (!message) {
            return "invalid task string";
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return *message;
    }
};

int main() {
    // Register simulation units and construct a runtime from the registry
    // Use two workers so two tasks in a job can run concurrently
    miaow::SimulationUnitRegistry simulation_unit_registry;
    const miaow::SimulationUnitId hello = simulation_unit_registry.add<HelloSimulationUnit>();

    miaow::RuntimeOptions runtime_options{/*workers=*/2};
    miaow::Runtime runtime(simulation_unit_registry, runtime_options);

    // Create two sets of tasks for separate jobs with two string arguments each
    std::vector<miaow::Task> first_job_tasks{
        miaow::Task{hello, "hello"},
        miaow::Task{hello, "cats"},
    };
    std::vector<miaow::Task> second_job_tasks{
        miaow::Task{hello, "hello again"},
        miaow::Task{hello, "more cats"},
    };

    // Submit both jobs and get their handles for collecting results when ready
    miaow::JobHandle first_job = runtime.submit_job(first_job_tasks);
    miaow::JobHandle second_job = runtime.submit_job(second_job_tasks);

    // Wait for both jobs to complete
    const miaow::JobResult first_job_result = first_job.get();
    const miaow::JobResult second_job_result = second_job.get();

    // Print the returned task strings
    for (const std::string& result : first_job_result.task_results_) {
        std::cout << result << "\n";
    }
    for (const std::string& result : second_job_result.task_results_) {
        std::cout << result << "\n";
    }

    // Print timing diagnostics for each job run
    miaow::print_diagnostics(first_job_result.diagnostics_, 0);
    miaow::print_diagnostics(second_job_result.diagnostics_, 1);

    return 0;
}
