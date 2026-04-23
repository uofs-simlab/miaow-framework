/**
 * This example runs C++ and Fortran BMI heat models through MIAOW's dynamic
 * BMI plugin path in one job. The two upstream heat examples do not solve the
 * same discrete update, so the example checks that both languages run through
 * the standard BMI simulation-unit path rather than comparing numeric output.
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

    // Print the dynamic libraries that will be loaded by each BMI wrapper
    const std::string cpp_heat_plugin_path = MIAOW_CPP_HEAT_PLUGIN_PATH;
    const std::string fortran_heat_plugin_path = MIAOW_FORTRAN_HEAT_PLUGIN_PATH;
    std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
    std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

    // Register dynamic C++ and Fortran BMI simulation units
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId cpp_heat =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicCppBmi>>(
            cpp_heat_plugin_path);
    const miaow::SimulationUnitId fortran_heat =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicFortranBmi>>(
            fortran_heat_plugin_path);
    miaow::Runtime runtime(registry, runtime_options);

    // Get the shared config file path for this dynamic heat workload
    const std::string heat_config = "./data/configs/config_dynamic_heat.txt";

    // Run one C++ heat task and one Fortran heat task in the same job
    const std::vector<miaow::Task> tasks{
        {cpp_heat, heat_config},
        {fortran_heat, heat_config},
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
