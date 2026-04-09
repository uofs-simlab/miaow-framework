/**
 * This example demonstrates how to run a multi-task SoilFreezeThaw workflow
 * with runtime CLI options.
 */

#include <iostream>
#include <string>
#include <vector>

#include "bmi_soil_freeze_thaw.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_cli.hpp"

int main(int argc, char* argv[]) {
    // Initialize runtime options and parse runtime-wide CLI arguments
    miaow::RuntimeOptions runtime_options;
    const std::string input_error = miaow::parse_runtime_cli_options(
        argc,
        argv,
        runtime_options);
    if (!input_error.empty()) {
        std::cerr << input_error << "\n";
        return 1;
    }

    // Register simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId soil_freeze_thaw =
        registry.add<miaow::BmiSimulationUnit<BmiSoilFreezeThaw>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Get config and forcing file paths for this SoilFreezeThaw workload
    const std::string standalone_config =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/configs/laramie_config_standalone.txt";
    const std::string short_config =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/configs/laramie_config_short_3_updates.txt";
    const std::string short_forcing =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/forcing/ground_temperature_forcing_config_3_rows.txt";
    const std::string trailing_rows_forcing =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/forcing/ground_temperature_forcing_config.txt";

    // Create three tasks with different combinations of config and forcing files
    const std::vector<miaow::Task> tasks{
        {soil_freeze_thaw, standalone_config},
        {soil_freeze_thaw, {short_config, short_forcing}},
        {soil_freeze_thaw, {short_config, trailing_rows_forcing}},
    };

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobResult job_result = job.get();

    // Print per-task status and message in completion order
    bool saw_failure = false;
    for (std::size_t index = 0; index < job_result.task_results_.size(); ++index) {
        const miaow::TaskResult& task_result = job_result.task_results_.at(index);
        std::cout << "[" << task_result.task_id_ << "] "
                  << miaow::task_status_to_string(task_result.status_) << ": "
                  << task_result.message_ << "\n";
        if (task_result.status_ != miaow::TaskStatus::succeeded) {
            saw_failure = true;
        }
    }

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_result.diagnostics_);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return saw_failure ? 1 : 0;
}
