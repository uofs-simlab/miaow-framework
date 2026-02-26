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
#include "miaow_interface.hpp" // CLI parser for example programs

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
        // Do nothing; just wait one second and return a greeting message with file path
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return std::string("Hello from 00_hello (config: ") + config_file + ")";
    }
};

int main(int argc, char* argv[]) {
   // Initialize runtime options and config file list to be populated by CLI parser
    miaow::RuntimeOptions runtime_options;
    std::vector<std::string> config_files;

    // Parse and validate command-line arguments
    const std::string input_error = miaow::parse_and_validate_workflow_cli_options(
        argc, argv, runtime_options, config_files);
    
    // Print any error in input arguments and output usage instructions if failed
    if (!input_error.empty()) {
        std::cerr << input_error << "\n" << miaow::workflow_cli_usage("miaow_hello") 
                  << "\n";
        return 1;
    }

    // Construct a runtime for a simulation unit of type HelloSimulationUnit
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
