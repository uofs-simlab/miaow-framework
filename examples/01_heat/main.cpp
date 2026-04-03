/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of
 * heat diffusion on a plate. The example uses the CSDMS BMI C++ heat example
 * implementation (`BmiHeat`) and a main program that sets up the MIAOW runtime
 * to execute the BMI job with a given configuration file.
 */

#include <iostream>
#include <string>
#include <vector>

#include "bmi_heat.hxx"     // defines the BmiHeat class that implements the BMI interface
#include "miaow.hpp"        // Core MIAOW runtime API
#include "miaow_bmi.hpp"    // MIAOW API for running BMI models as simulation units
#include "miaow_cli.hpp" // CLI parser for example programs

/**
 * @brief A simple simulation unit used to return a hello message. This is included to
 * demonstrate that multiple simulation units can be mixed in the same job.
 * In a real workflow, this could be replaced with other simulation units that perform
 * different tasks, such as data preprocessing, postprocessing, or running different models.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    miaow::TaskResult execute(const miaow::Task& task) override {
        miaow::TaskResult task_result;

        if (task.args_.empty()) {
            task_result.status_ = miaow::TaskStatus::succeeded;
            task_result.message_ = "hello";
            return task_result;
        }

        const std::string* message = caf::get_if<std::string>(&task.args_.front());
        task_result.status_ = miaow::TaskStatus::succeeded;
        task_result.message_ = message ? *message : "hello";
        return task_result;
    }
};

static std::string task_status_string(miaow::TaskStatus status) {
    if (status == miaow::TaskStatus::succeeded) {
        return "succeeded";
    }
    return "failed";
}

int main(int argc, char* argv[]) {
    // Initialize runtime options and config file list to be populated by CLI parser
    miaow::RuntimeOptions runtime_options;
    std::vector<std::string> config_files;
    std::vector<std::string> forcing_config_files;

    // Parse and validate command-line arguments
    const std::string input_error = miaow::parse_and_validate_workflow_cli_options(
        argc, argv, runtime_options, config_files, forcing_config_files);

    // Print any error in input arguments and output usage instructions if failed
    if (!input_error.empty()) {
        std::cerr << input_error << "\n" << miaow::workflow_cli_usage("miaow_heat") << "\n";
        return 1;
    }

    // Register BMI simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry simulation_unit_registry;
    const miaow::SimulationUnitId hello =
        simulation_unit_registry.add<HelloSimulationUnit>();
    const miaow::SimulationUnitId heat =
        simulation_unit_registry.add<miaow::BmiSimulationUnit<BmiHeat>>();
    miaow::Runtime runtime(simulation_unit_registry, runtime_options);

    // Create a vector of tasks to be executed
    std::vector<miaow::Task> tasks;

    // Each task corresponds to running the BMI model with one of the provided config files
    tasks.reserve(config_files.size());
    for (std::size_t index = 0; index < config_files.size(); ++index) {
        const std::string& config_file = config_files.at(index);
        if (forcing_config_files.empty()) {
            tasks.emplace_back(heat, config_file);
            continue;
        }

        std::vector<caf::config_value> task_args;
        task_args.reserve(2);
        task_args.emplace_back(config_file);
        task_args.emplace_back(forcing_config_files.at(index));
        tasks.emplace_back(heat, std::move(task_args));
    }

    // Add one hello task to demonstrate mixed simulation-unit routing in the same job
    tasks.emplace_back(hello, "hello");

    // Submit one job and continue (does not block)
    miaow::JobHandle job = runtime.submit_job(tasks);

    // Wait for the job to complete and get the results when ready
    const miaow::JobResult job_result = job.get();

    // Print the results for each config file in order
    for (std::size_t index = 0; index < job_result.task_results_.size(); ++index) {
        const miaow::TaskResult& task_result = job_result.task_results_.at(index);
        std::cout << "[" << index << "] "
                  << task_status_string(task_result.status_) << ": "
                  << task_result.message_ << "\n";
    }

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_result.diagnostics_);

    return 0;
}
