/**
 * Run C++ and Fortran heat models through the dynamic BMI plugin interface.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"

int main(int argc, char* argv[]) {
    // Require the example directory so relative input paths resolve correctly
    if (!miaow::ensure_expected_run_dir(MIAOW_DYNAMIC_BMI_RUN_DIR)) {
        return 1;
    }

    // Read optional plugin inputs and the common runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);

        // Resolve plugin paths and shared model settings from the optional config
        const std::string cpp_heat_plugin_path =
            config.value("cpp_plugin", MIAOW_CPP_HEAT_PLUGIN_PATH);
        const std::string fortran_heat_plugin_path =
            config.value("fortran_plugin", MIAOW_FORTRAN_HEAT_PLUGIN_PATH);

        // Select the shared initialization file and model-time tolerance
        const std::string heat_config =
            config.value("init_config", "./data/configs/config_dynamic_heat.txt");
        const double tolerance_seconds =
            config.value("time_step_tolerance_seconds",
                         miaow::default_bmi_time_step_tolerance_seconds);

        // Show which plugins will run through both execution backends
        std::cout << "C++ dynamic BMI plugin: " << cpp_heat_plugin_path << "\n";
        std::cout << "Fortran dynamic BMI plugin: " << fortran_heat_plugin_path << "\n";

        // Register in-process and subprocess execution
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId dynamic_bmi =
            registry.add<miaow::DynamicBmiTaskExecutor>();
        const miaow::TaskExecutorId subprocess_bmi =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path());
        miaow::Runtime runtime(registry, runtime_options);

        // Pair each plugin with the shared model settings
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

        // Each configuration runs once in-process and once in its own subprocess
        // Each SimulationUnit owns its own executor instance
        miaow::Job job;
        for (const miaow::TaskExecutorId executor : {dynamic_bmi, subprocess_bmi}) {
            const std::string mode =
                executor == dynamic_bmi ? "in_process_" : "subprocess_";
            for (std::size_t index = 0; index < configs.size(); ++index) {
                const miaow::SimulationUnitHandle unit =
                    job.add_unit(mode + std::to_string(index), executor);
                job.add_task(miaow::make_bmi_task(unit, configs[index]));
            }
        }

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord job_record = job_handle.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
