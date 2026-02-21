/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit 
 * and runtime. This example defines a HelloSimulationUnit that returns a greeting message
 * when executed, and uses the Runtime API to execute it. The config file path is passed to
 * the simulation unit's execute method, where model-specific logic would be implemented. 
*/

#include <iostream>
#include <memory>
#include <string>

#include "miaow.hpp"

class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    /**
     * Override the execute method to return a greeting message. In a real simulation unit,
     * this is where you would implement the logic to initialize and run your model, using
     * the config file path as needed.
     */
    std::string execute(const std::string& config_file) override {
        return std::string("Hello from 00_hello (config: ") + config_file + ")";
    }
};

int main(int argc, char* argv[]) {
    // If nothing else provided, default to "config.txt" in the current directory.
    // Otherwise, use the first command line argument as the config file path.
    std::string config_file = "config.txt";
    if (argc > 1) {
        config_file = argv[1];
    }

    // Example-specific simulation unit subtype dispatched through the Runtime API.
    std::shared_ptr<miaow::SimulationUnit> simulation_unit =
        std::make_shared<HelloSimulationUnit>();

    // Set up runtime options if desired, or just use the defaults.
    miaow::RuntimeOptions runtime_options;

    // Construct a runtime using the desired simulation unit and runtime options
    miaow::Runtime runtime(simulation_unit, runtime_options);

    // Run the workflow with the config file path, and print the result.
    std::cout << runtime.run_workflow(config_file) << "\n";
    return 0;
}
