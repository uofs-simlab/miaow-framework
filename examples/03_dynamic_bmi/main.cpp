/**
 * This example runs C++ and Fortran BMI heat models through MIAOW's dynamic
 * BMI plugin path in one job. The two upstream heat examples do not solve the
 * same discrete update, so the example checks that both languages run through
 * the standard BMI simulation-unit path rather than comparing numeric output.
 */

#include <iostream>
#include <string>
#include <vector>

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

    // Print the dynamic libraries that will be loaded by each BMI wrapper
    const std::string cpp_heat_plugin_path = MIAOW_CPP_HEAT_PLUGIN_PATH;
    const std::string fortran_heat_plugin_path = MIAOW_FORTRAN_HEAT_PLUGIN_PATH;
    std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
    std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

    // Register dynamic C++ and Fortran BMI simulation units
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId cpp_heat =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicCppBmiModel>>(
            cpp_heat_plugin_path);
    const miaow::SimulationUnitId fortran_heat =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicFortranBmiModel>>(
            fortran_heat_plugin_path);
    miaow::Runtime runtime(registry, runtime_options);

    // Get the shared config file path for this dynamic heat workload
    const std::string heat_config = MIAOW_DYNAMIC_HEAT_CONFIG_PATH;

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
