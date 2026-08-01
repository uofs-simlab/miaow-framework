/**
 * Run one SoilFreezeThaw BMI task with aligned forcing data.
 */

#include <exception>
#include <iostream>

#include "bmi_soil_freeze_thaw.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_SOIL_FREEZE_THAW_RUN_DIR)) {
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

    // Register simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId soil_freeze_thaw =
        registry.add<miaow::StaticBmiSimulationUnit<BmiSoilFreezeThaw>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Pair one model configuration with matching one-day forcing data and input mapping
    const miaow::StaticBmiConfig config{
        .init_config_ = "./data/configs/laramie_config_1_day.txt",
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = "./data/forcings/Laramie_14Jun09_to_15Jun09.csv",
            .variables_names_map_ = {{"ground_temperature", "TMP_ground_surface"}},
        },
    };
    const miaow::Task task = miaow::make_bmi_task(soil_freeze_thaw, config);

    // Submit a job containing the BMI task and wait for its complete lifecycle
    miaow::JobHandle job = runtime.submit_job(task);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
