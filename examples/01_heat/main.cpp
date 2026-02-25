/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of 
 * heat diffusion on a plate. The example includes a custom BMI implementation (BmiHeat) 
 * that wraps a simple heat diffusion model, and a main program that sets up the MIAOW 
 * runtime to execute the BMI workflow with a given configuration file.
 */

#include <iostream>
#include <chrono>
#include <string>
#include <vector>

#include "bmi_heat.hxx"     // defines the BmiHeat class that implements the BMI interface
#include "miaow.hpp"        // Core MIAOW runtime API
#include "miaow_bmi.hpp"    // MIAOW API for running BMI models as simulation units

int main(int argc, char* argv[]) {
    // If nothing else is provided, default to one local config
    // Otherwise, treat all command line arguments as independent config files
    std::vector<std::string> config_files;
    if (argc <= 1) {
        config_files.emplace_back("config.txt");
    } else {
        for (int index = 1; index < argc; ++index) {
            config_files.emplace_back(argv[index]);
        }
    }

    // Use a larger timeout to avoid false timeouts on heavier grids
    miaow::RuntimeOptions runtime_options{std::chrono::seconds(30), config_files.size()};
    miaow::Runtime runtime =
        miaow::make_runtime<miaow::BmiSimulationUnit<BmiHeat>>(runtime_options);

    const std::vector<std::string> results = runtime.run_workflow(config_files);
    for (std::size_t index = 0; index < results.size(); ++index) {
        std::cout << "[" << index << "] " << results[index] << "\n";
    }

    return 0;
}
