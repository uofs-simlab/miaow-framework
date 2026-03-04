/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of 
 * heat diffusion on a plate. The example includes a custom BMI implementation (BmiHeat) 
 * that wraps a simple heat diffusion model, and a main program that sets up the MIAOW 
 * runtime to execute the BMI job with a given configuration file.
 */

#include <iostream>
#include <string>
#include <vector>

#include "bmi_heat.hxx"     // defines the BmiHeat class that implements the BMI interface
#include "miaow.hpp"        // Core MIAOW runtime API
#include "miaow_bmi.hpp"    // MIAOW API for running BMI models as simulation units
#include "miaow_interface.hpp" // CLI parser for example programs

int main(int argc, char* argv[]) {
    // Initialize runtime options and config file list to be populated by CLI parser
    miaow::RuntimeOptions runtime_options;
    std::vector<std::string> config_files;

    // Parse and validate command-line arguments
    const std::string input_error = miaow::parse_and_validate_workflow_cli_options(
        argc, argv, runtime_options, config_files);
    
    // Print any error in input arguments and output usage instructions if failed
    if (!input_error.empty()) {
        std::cerr << input_error << "\n" << miaow::workflow_cli_usage("miaow_heat") << "\n";
        return 1;
    }

    // Create a MIAOW runtime with a BMI simulation unit that uses the BmiHeat model
    miaow::Runtime runtime =
        miaow::make_runtime<miaow::BmiSimulationUnit<BmiHeat>>(runtime_options);

    // Create a vector of tasks to be executed
    std::vector<miaow::Task> tasks;

    // Each task corresponds to running the BMI model with one of the provided config files
    tasks.reserve(config_files.size());
    for (const std::string& config_file : config_files) {
        tasks.emplace_back(config_file);
    }

    // Submit one job and continue (does not block)
    miaow::JobHandle job = runtime.submit_job(tasks);

    // Wait for the job to complete and get the results when ready
    const miaow::JobResult job_result = job.get();

    // Print the results for each config file in order
    for (std::size_t index = 0; index < job_result.task_results.size(); ++index) {
        std::cout << "[" << index << "] " << job_result.task_results[index] << "\n";
    }

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_result.diagnostics);

    return 0;
}
