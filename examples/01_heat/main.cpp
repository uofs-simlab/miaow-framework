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
    // Fill a vector with config file paths from command line arguments
    std::vector<std::string> config_files;
    for (int index = 1; index < argc; ++index) {
        config_files.emplace_back(argv[index]);
    }

    // Validate config paths and fail on missing/invalid input
    const std::string validation_error = miaow::validate_config_paths(config_files);
    if (!validation_error.empty()) {
        std::cerr << validation_error << "\n";
        std::cerr << "Usage: miaow_heat <config_file_1> [<config_file_2> ...]\n";
        return 1;
    }

    // Create a MIAOW runtime with a BMI simulation unit that uses the BmiHeat model
    miaow::RuntimeOptions runtime_options{
        config_files.size(),          // max number of workers
        std::chrono::seconds(30),     // task timeout limit
        std::chrono::minutes(5)       // job timeout limit
    };
    miaow::Runtime runtime =
        miaow::make_runtime<miaow::BmiSimulationUnit<BmiHeat>>(runtime_options);

    // Run one job that may contain one or many independent simulation units
    const miaow::WorkflowResult workflow_result = runtime.run_workflow(config_files);
    
    // Print the results for each config file in order
    for (std::size_t index = 0; index < workflow_result.task_results.size(); ++index) {
        std::cout << "[" << index << "] " << workflow_result.task_results[index] << "\n";
    }

    // Print timing diagnostics for this workflow run
    miaow::print_diagnostics(workflow_result.diagnostics);

    return 0;
}
