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
    //   1: 512-day model, 1-day forcing (forcing exhausts before end time, expect failure)
    //   2: 1-day model, 512-day forcing (model stops before forcing ends, expect success)
    const std::vector<miaow::StaticBmiConfig> configs{
        {.init_config_ = config_512_days,
         .forcing_file_ = forcing_512_days,
         .variables_names_map_ = forcing_names_map},
        {.init_config_ = config_512_days,
         .forcing_file_ = forcing_1_day,
         .variables_names_map_ = forcing_names_map},
        {.init_config_ = config_1_day,
         .forcing_file_ = forcing_512_days,
         .variables_names_map_ = forcing_names_map},
    };
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(soil_freeze_thaw, configs);

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_record);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_record);

    // Verify the two valid cases and the expected forcing exhaustion
    const bool results_match =
        job_record.tasks_.size() == 3 &&
        job_record.tasks_.at(0).result_.status_ == miaow::TaskStatus::succeeded &&
        job_record.tasks_.at(1).result_.status_ == miaow::TaskStatus::failed &&
        job_record.tasks_.at(1).result_.message_.starts_with(
            "Forcing alignment error: forcing series exhausted") &&
        job_record.tasks_.at(2).result_.status_ == miaow::TaskStatus::succeeded;
    return results_match ? 0 : 1;
}
