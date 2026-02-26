/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit 
 * and runtime. This example defines a HelloSimulationUnit that returns a greeting message
 * when executed, and uses the Runtime API to execute it. The config file path is passed to
 * the simulation unit's execute method, where model-specific logic would be implemented. 
*/

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include "miaow.hpp" // Core MIAOW runtime API

/**
 * @brief A simple simulation unit that returns a greeting message.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    /**
     * @brief Override the execute method to return a greeting message. 
     * In a real simulation unit, this is where you would implement the logic to initialize
     * and run your model.
     * @param config_file A string representing the path to the model configuration file. 
     * @return A greeting message that includes the config file path.
     */
    std::string execute(const std::string& config_file) override {
        // Simulate one second of work for each simulation unit task
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return std::string("Hello from 00_hello (config: ") + config_file + ")";
    }
};

int main(int argc, char* argv[]) {
    // Fill a vector with config file paths from command line arguments
    std::vector<std::string> config_files;
    for (int index = 1; index < argc; ++index) {
        config_files.emplace_back(argv[index]);
    }

    // Validate config paths and fail on missing/invalid input
    const std::string validation_error = miaow::validate_config_paths(config_files);
    if (!validation_error.empty()) {
        std::cerr << validation_error << "\n";
        std::cerr << "Usage: miaow_hello <config_file_1> [<config_file_2> ...]\n";
        return 1;
    }

    // Construct a runtime for a simulation unit of type HelloSimulationUnit
    miaow::RuntimeOptions runtime_options{
        config_files.size(),       // max number of workers
        std::chrono::seconds(5),   // task timeout limit
        std::chrono::seconds(20)   // job timeout limit
    };
    miaow::Runtime runtime = miaow::make_runtime<HelloSimulationUnit>(runtime_options);

    // Run one job that may contain one or many independent simulation units
    const miaow::WorkflowResult workflow_result = runtime.run_workflow(config_files);

    // Print the results from each config file in order
    for (std::size_t index = 0; index < workflow_result.task_results.size(); ++index) {
        std::cout << "[" << index << "] " << workflow_result.task_results[index] << "\n";
    }

    // Print timing diagnostics for this workflow run
    miaow::print_diagnostics(workflow_result.diagnostics);

    return 0;
}
