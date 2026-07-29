/**
 * Run C++ and Fortran heat models through the dynamic BMI plugin interface.
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

    // Register one simulation unit, to be reused for all dynamic plugin tasks
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    // Configure one in-process task for each supported plugin language
    const std::vector<miaow::DynamicBmiConfig> configs{
        {.backend_ = miaow::DynamicBmiBackend::cpp,
         .plugin_path_ = cpp_heat_plugin_path,
         .init_config_ = heat_config},
        {.backend_ = miaow::DynamicBmiBackend::fortran,
         .plugin_path_ = fortran_heat_plugin_path,
         .init_config_ = heat_config},
    };

    // Build both tasks through the same dynamic simulation unit
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(dynamic_bmi, configs);

    // Submit a job containing the plugin tasks and wait for both BMI lifecycles
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
