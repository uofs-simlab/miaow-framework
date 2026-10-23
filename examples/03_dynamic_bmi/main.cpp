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
#include "../config.hpp"

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_DYNAMIC_BMI_RUN_DIR)) {
        return 1;
    }

    // Read optional plugin inputs and the common runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        const auto config = read_example_config(argc, argv, runtime_options);

        // Print the dynamic libraries that will be loaded by BMI tasks
        const std::string cpp_heat_plugin_path =
            config.value("cpp_plugin", MIAOW_CPP_HEAT_PLUGIN_PATH);
        const std::string fortran_heat_plugin_path =
            config.value("fortran_plugin", MIAOW_FORTRAN_HEAT_PLUGIN_PATH);
        std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
        std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

        // Describe the shared heat initialization config
        const std::string heat_config =
            config.value("init_config", "./data/configs/config_dynamic_heat.txt");
        const double tolerance_seconds =
            config.value("time_step_tolerance_seconds",
                         miaow::default_bmi_time_step_tolerance_seconds);

        // Register in-process and subprocess execution
        miaow::SimulationUnitRegistry registry;
        const miaow::SimulationUnitId dynamic_bmi =
            registry.add<miaow::DynamicBmiSimulationUnit>();
        const miaow::SimulationUnitId subprocess_bmi =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path(),
                                    miaow::SubprocessLifetime::single_task);
        miaow::Runtime runtime(registry, runtime_options);

        // Describe each plugin and its initialization file
        const std::vector<miaow::DynamicBmiConfig> configs{
            {.backend_ = miaow::DynamicBmiBackend::cpp,
             .plugin_path_ = cpp_heat_plugin_path,
             .init_config_ = heat_config,
             .time_step_tolerance_seconds_ = tolerance_seconds},
            {.backend_ = miaow::DynamicBmiBackend::fortran,
             .plugin_path_ = fortran_heat_plugin_path,
             .init_config_ = heat_config,
             .time_step_tolerance_seconds_ = tolerance_seconds},
        };

        // Run each plugin in-process and in a fresh subprocess
        std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(dynamic_bmi, configs);
        const auto subprocess_tasks = miaow::make_bmi_tasks(subprocess_bmi, configs);
        tasks.insert(tasks.end(), subprocess_tasks.begin(), subprocess_tasks.end());

        // Wait for all four BMI lifecycles
        miaow::JobHandle job = runtime.submit_job(tasks);
        const miaow::JobRecord job_record = job.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
