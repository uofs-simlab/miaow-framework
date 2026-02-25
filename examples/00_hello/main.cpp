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

#include "miaow.hpp"

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
    // If nothing else is provided, default to one local config
    // Otherwise, treat all command line arguments as independent config files
    std::vector<std::string> config_files;
    if (argc <= 1) {
        config_files.push_back("config.txt");
    } else {
        for (int index = 1; index < argc; ++index) {
            config_files.emplace_back(argv[index]);
        }
    }

    // Configure the runtime options for this example
    miaow::RuntimeOptions runtime_options{std::chrono::seconds(5), // request timeout
                                          config_files.size()};    // max workers

    // Construct a runtime for a simulation unit of type HelloSimulationUnit
    miaow::Runtime runtime =
        miaow::make_runtime<HelloSimulationUnit>(runtime_options);

    // Run one job that may contain one or many independent simulation units
    const std::vector<std::string> results = runtime.run_workflow(config_files);

    // Print the results from each simulation unit
    for (std::size_t index = 0; index < results.size(); ++index) {
        std::cout << "[" << index << "] " << results[index] << "\n";
    }
    return 0;
}
