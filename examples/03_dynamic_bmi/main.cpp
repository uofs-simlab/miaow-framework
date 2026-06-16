/**
 * This example runs C++ and Fortran BMI heat models through MIAOW's dynamic
 * BMI plugin path in one job. The two upstream heat examples do not solve the
 * same discrete update, so the example checks that both languages run through
 * the single-model BMI pipeline path rather than comparing numeric output.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

#ifndef MIAOW_FIRST_PERSISTENT_SUBPROCESS_PATH
#define MIAOW_FIRST_PERSISTENT_SUBPROCESS_PATH MIAOW_SUBPROCESS_PATH
#endif

#ifndef MIAOW_EXPECT_FIRST_PERSISTENT_FAILURE
#define MIAOW_EXPECT_FIRST_PERSISTENT_FAILURE 0
#endif

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir("miaow_dynamic_bmi", MIAOW_DYNAMIC_BMI_RUN_DIR)) {
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

    // Print the dynamic libraries that will be loaded by pipeline modules
    const std::string cpp_heat_plugin_path = MIAOW_CPP_HEAT_PLUGIN_PATH;
    const std::string fortran_heat_plugin_path = MIAOW_FORTRAN_HEAT_PLUGIN_PATH;
    std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
    std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

    // Describe the shared heat initialization config
    const std::string heat_config = "./data/configs/config_dynamic_heat.txt";

    // Register one pipeline unit for every dynamic plugin-backed task
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId heat_pipeline =
        registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Run plugin-backed pipelines in-process and through explicit subprocess paths
    std::vector<miaow::Task> tasks{
        miaow::make_dynamic_bmi_task(heat_pipeline, "bmi_cpp",
                                     miaow::DynamicBmiBackend::cpp,
                                     cpp_heat_plugin_path, heat_config),
        miaow::make_dynamic_bmi_task(heat_pipeline, "bmi_fortran",
                                     miaow::DynamicBmiBackend::fortran,
                                     fortran_heat_plugin_path, heat_config),
    };

    miaow::Task first_cpp_heat_subprocess_task = miaow::make_dynamic_bmi_task(
        heat_pipeline, "bmi_cpp", miaow::DynamicBmiBackend::cpp,
        cpp_heat_plugin_path, heat_config, {},
        miaow::ExecutionMode::persistent_subprocess);
    first_cpp_heat_subprocess_task.subprocess_path_ =
        MIAOW_FIRST_PERSISTENT_SUBPROCESS_PATH;
    tasks.push_back(first_cpp_heat_subprocess_task);

    miaow::Task single_cpp_heat_subprocess_task = miaow::make_dynamic_bmi_task(
        heat_pipeline, "bmi_cpp", miaow::DynamicBmiBackend::cpp,
        cpp_heat_plugin_path, heat_config, {},
        miaow::ExecutionMode::single_task_subprocess);
    single_cpp_heat_subprocess_task.subprocess_path_ = MIAOW_SUBPROCESS_PATH;
    tasks.push_back(single_cpp_heat_subprocess_task);

    miaow::Task second_cpp_heat_subprocess_task = miaow::make_dynamic_bmi_task(
        heat_pipeline, "bmi_cpp", miaow::DynamicBmiBackend::cpp,
        cpp_heat_plugin_path, heat_config, {},
        miaow::ExecutionMode::persistent_subprocess);
    second_cpp_heat_subprocess_task.subprocess_path_ = MIAOW_SUBPROCESS_PATH;
    tasks.push_back(second_cpp_heat_subprocess_task);

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_record);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_record);

#if MIAOW_EXPECT_FIRST_PERSISTENT_FAILURE
    // The recovery test expects the first persistent launch to fail
    const bool expected_recovery =
        job_record.tasks_.size() == 5 &&
        job_record.tasks_.at(0).result_.status_ == miaow::TaskStatus::succeeded &&
        job_record.tasks_.at(1).result_.status_ == miaow::TaskStatus::succeeded &&
        job_record.tasks_.at(2).result_.status_ == miaow::TaskStatus::failed &&
        job_record.tasks_.at(3).result_.status_ == miaow::TaskStatus::succeeded &&
        job_record.tasks_.at(4).result_.status_ == miaow::TaskStatus::succeeded;
    return expected_recovery ? 0 : 1;
#else
    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
#endif
}
