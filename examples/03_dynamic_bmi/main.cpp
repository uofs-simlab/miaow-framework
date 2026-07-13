/**
 * This example runs C++ and Fortran BMI heat models through MIAOW's dynamic
 * BMI plugin path in one job. The two upstream heat examples do not solve the
 * same discrete update, so the example checks that both languages run through
 * the same BMI task path rather than comparing numeric output.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_DYNAMIC_BMI_RUN_DIR)) {
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

    // Print the dynamic libraries that will be loaded by BMI tasks
    const std::string cpp_heat_plugin_path = MIAOW_CPP_HEAT_PLUGIN_PATH;
    const std::string fortran_heat_plugin_path = MIAOW_FORTRAN_HEAT_PLUGIN_PATH;
    std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
    std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

    // Describe the shared heat initialization config
    const std::string heat_config = "./data/configs/config_dynamic_heat.txt";

    // Register one simulation unit for every dynamic plugin task
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    const std::vector<miaow::DynamicBmiConfig> configs{
        // Run the C++ plugin in the current process
        {.backend_ = miaow::DynamicBmiBackend::cpp,
         .plugin_path_ = cpp_heat_plugin_path,
         .init_config_ = heat_config},
        // Run the Fortran plugin in the current process
        {.backend_ = miaow::DynamicBmiBackend::fortran,
         .plugin_path_ = fortran_heat_plugin_path,
         .init_config_ = heat_config},
        // Run the C++ plugin in a persistent subprocess
        {.backend_ = miaow::DynamicBmiBackend::cpp,
         .plugin_path_ = cpp_heat_plugin_path,
         .init_config_ = heat_config,
         .execution_mode_ = miaow::ExecutionMode::persistent_subprocess},
        // Run one C++ plugin task in a temporary subprocess
        {.backend_ = miaow::DynamicBmiBackend::cpp,
         .plugin_path_ = cpp_heat_plugin_path,
         .init_config_ = heat_config,
         .execution_mode_ = miaow::ExecutionMode::single_task_subprocess},
    };
    const std::vector<miaow::Task> tasks =
        miaow::make_bmi_tasks(dynamic_bmi, configs);

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
