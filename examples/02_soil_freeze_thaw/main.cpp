/**
 * This example demonstrates how to run a multi-task SoilFreezeThaw workflow
 * with runtime CLI options, exercising matched and mismatched combinations of
 * model end time and forcing series length.
 */

#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "bmi_soil_freeze_thaw.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir("miaow_soil_freeze_thaw",
                                        MIAOW_SOIL_FREEZE_THAW_RUN_DIR)) {
        return 1;
    }

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
    const std::string config_512_days = "./data/configs/laramie_config_512_days.txt";
    const std::string config_1_day = "./data/configs/laramie_config_1_day.txt";
    const std::string forcing_1_day = "./data/forcings/Laramie_14Jun09_to_15Jun09.csv";
    const std::string forcing_512_days = "./data/forcings/Laramie_14Jun09_to_08Nov10.csv";
    const std::map<std::string, std::string> forcing_names_map{
        {"ground_temperature", "TMP_ground_surface"},
    };

    // Three cases exercising matched and mismatched config/forcing durations:
    //   0: 512-day model, 512-day forcing (exact match)
    //   1: 512-day model, 1-day forcing (forcing exhausts before end time)
    //   2: 1-day model, 512-day forcing (model stops before forcing ends)
    std::vector<miaow::Task> tasks{
        miaow::Task{soil_freeze_thaw,
                    miaow::bmi_pipeline_config_to_value(miaow::make_bmi_model_config(
                        config_512_days, forcing_512_days, forcing_names_map)),
                    miaow::TaskMetadata{{"label", "matched-duration"},
                                        {"config_path", config_512_days},
                                        {"forcing_file", forcing_512_days}}},
        miaow::Task{soil_freeze_thaw,
                    miaow::bmi_pipeline_config_to_value(miaow::make_bmi_model_config(
                        config_512_days, forcing_1_day, forcing_names_map)),
                    miaow::TaskMetadata{{"label", "short-forcing"},
                                        {"config_path", config_512_days},
                                        {"forcing_file", forcing_1_day}}},
        miaow::Task{soil_freeze_thaw,
                    miaow::bmi_pipeline_config_to_value(miaow::make_bmi_model_config(
                        config_1_day, forcing_512_days, forcing_names_map)),
                    miaow::TaskMetadata{{"label", "long-forcing"},
                                        {"config_path", config_1_day},
                                        {"forcing_file", forcing_512_days}}},
    };

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
