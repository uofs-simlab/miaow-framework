/**
 * Run one statically linked C++ BMI heat model through MIAOW.
 */

#include <exception>
#include <iostream>

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

    // Build one task from the model initialization file
    const miaow::StaticBmiConfig heat_config{
        .init_config_ = "./data/configs/config_400x400.txt",
    };
    const miaow::Task task = miaow::make_bmi_task(heat, heat_config);

    // Submit a job containing the BMI task and wait for its complete lifecycle
    miaow::JobHandle job = runtime.submit_job(task);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
