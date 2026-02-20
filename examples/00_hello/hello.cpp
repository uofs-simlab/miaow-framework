/**
 * A simple "Hello World" example demonstrating the basic structure of a simulation unit 
 * and runtime. This example defines a HelloSimulationUnit that returns a greeting message
 * when run, and uses the Runtime API to execute it. The config file path is passed to the 
 * simulation unit's run method, where the model-specific logic would be implemented. 
*/

#include <iostream>
#include <memory>
#include <string>

#include "miaow.hpp"

class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    std::string run(const std::string& config_file) override {
        return std::string("Hello from 00_hello (config: ") + config_file + ")";
    }
};

int main(int argc, char* argv[]) {
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
