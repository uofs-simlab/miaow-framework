/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of
 * heat diffusion on a plate. The example uses the CSDMS BMI C++ heat example
 * implementation (`BmiHeat`) and accepts runtime CLI options.
 */

#include <exception>
#include <iostream>
#include <vector>

#include "bmi_heat.hxx" // defines the BmiHeat class that implements the BMI interface

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_bmi.hpp" // MIAOW API for running BMI models as simulation units
#include "miaow_io.hpp"  // I/O helpers for example programs

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_HEAT_RUN_DIR)) {
        return 1;
    }

    // Initialize runtime options and parse runtime CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register the statically typed BMI unit and build runtime routing
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId heat =
        registry.add<miaow::StaticBmiSimulationUnit<BmiHeat>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Build the statically typed BMI tasks from their model configs
    const std::vector<miaow::StaticBmiConfig> heat_configs{
        {.init_config_ = "./data/configs/config_400x400.txt"},
        {.init_config_ = "./data/configs/config_800x800.txt"},
    };
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(heat, heat_configs);

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_record);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_record);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
