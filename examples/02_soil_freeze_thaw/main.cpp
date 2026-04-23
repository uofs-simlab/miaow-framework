/**
 * This example demonstrates how to run a multi-task SoilFreezeThaw workflow
 * with runtime CLI options, exercising matched and mismatched combinations of
 * model end time and forcing series length.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "bmi_soil_freeze_thaw.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Initialize runtime options and parse runtime-wide CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId soil_freeze_thaw =
        registry.add<miaow::BmiSimulationUnit<BmiSoilFreezeThaw>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Get config and forcing file paths for this SoilFreezeThaw workload
    const std::string config_1024_days =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/configs/laramie_config_1024_days.txt";
    const std::string config_1_day =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/configs/laramie_config_1_day.txt";
    const std::string forcing_1024_days =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/forcings/ground_temperature_forcing_config_1024_days.txt";
    const std::string forcing_1_day =
        std::string(MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR) +
        "/forcings/ground_temperature_forcing_config_1_day.txt";

    // Three cases exercising matched and mismatched config/forcing durations:
    //   0: 1024-day model, 1024-day forcing (exact match)
    //   1: 1024-day model, 1-day forcing (forcing exhausts before end time)
    //   2: 1-day    model, 1024-day forcing (model stops before forcing ends)
    const std::vector<miaow::Task> tasks{
        {soil_freeze_thaw, {config_1024_days, forcing_1024_days}},
        {soil_freeze_thaw, {config_1024_days, forcing_1_day}},
        {soil_freeze_thaw, {config_1_day, forcing_1024_days}},
    };

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobResult job_result = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_result);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_result.diagnostics_);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(job_result) ? 0 : 1;
}
