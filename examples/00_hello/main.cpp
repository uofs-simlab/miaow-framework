/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit
 * and runtime. This example defines a HelloSimulationUnit that returns an ASCII cat
 * message when executed and uses the Runtime API to execute it.
*/

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "miaow.hpp" // Core MIAOW runtime API

/**
 * @brief A simple simulation unit that returns an ASCII cat message.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    /**
     * @brief Override the execute method to return ASCII cat art.
     * In a real simulation unit, this is where you would implement the logic to initialize
     * and run your model.
     * @param task Unused task payload in this toy example.
     * @return A multiline message with ASCII cat art.
     */
    std::string execute(const miaow::Task& /*task*/) override {
        // Do nothing; just wait one second and return a cat message
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return "miaow!\n"
               " /\\_/\\\n"
               "(='.'=)\n"
               "(\")_(\")";
    }
};

int main() {
    // Construct a runtime for a simulation unit of type HelloSimulationUnit
    miaow::Runtime runtime = miaow::make_runtime<HelloSimulationUnit>({});

    std::vector<miaow::Task> tasks{
        miaow::Task{"hello"},
        miaow::Task{"cats"},
    };

    // Run one job that may contain one or many independent simulation units
    const miaow::WorkflowResult workflow_result = runtime.run_workflow(tasks);

    // Print the results from each input message in order
    for (std::size_t index = 0; index < workflow_result.task_results.size(); ++index) {
        std::cout << "[" << index << "] " << workflow_result.task_results[index] << "\n";
    }

    // Print timing diagnostics for this workflow run
    miaow::print_diagnostics(workflow_result.diagnostics);

    return 0;
}
