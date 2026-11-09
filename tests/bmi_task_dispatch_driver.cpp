// Execute BMI operation sequences for validation through the saved job record
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "bmi_heat.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "../examples/config.hpp"

int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions runtime_options;
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);
        const std::string operation = config.value("operation", "unknown");
        const std::string scenario = config.value("scenario", "dispatch");
        const std::string init_config =
            config.value("init_config", "./data/configs/config_dynamic_heat.txt");

        // Reuse the example's heat models across all execution paths
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId static_bmi =
            registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
        const miaow::TaskExecutorId dynamic_bmi =
            registry.add<miaow::DynamicBmiTaskExecutor>();
        const miaow::TaskExecutorId subprocess_bmi =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path());

        // Follow the configured operation with a valid run on the same unit
        miaow::Job job;
        const auto add_operation_pair = [&job, &operation](miaow::Task run_task) {
            miaow::Task first_task = run_task;
            first_task.operation_ = operation;
            job.add_task(std::move(first_task));
            job.add_task(std::move(run_task));
        };

        caf::config_value::dictionary model_config;
        model_config.insert("init_config", init_config);

        if (scenario == "static_lifecycle" || scenario == "dynamic_lifecycle") {
            const bool dynamic = scenario == "dynamic_lifecycle";
            const miaow::TaskExecutorId dynamic_executor =
                config.value("subprocess", false) ? subprocess_bmi : dynamic_bmi;
            const miaow::SimulationUnitHandle unit =
                job.add_unit(dynamic ? "dynamic" : "static",
                             dynamic ? dynamic_executor : static_bmi);
            if (dynamic) {
                const bool fortran = config.value("backend", "cpp") == "fortran";
                model_config.insert("backend", fortran ? "fortran" : "cpp");
                model_config.insert("plugin_path", fortran ? MIAOW_FORTRAN_HEAT_PLUGIN_PATH
                                                           : MIAOW_CPP_HEAT_PLUGIN_PATH);
            }

            // Rejected operations preserve the active model; finalization permits reuse
            job.add_task(unit, "update");
            job.add_task(unit, "initialize", model_config);
            job.add_task(unit, "update");
            job.add_task(unit, "initialize", model_config);
            job.add_task(unit, "run", model_config);
            job.add_task(unit, "get_current_time");
            job.add_task(unit, "finalize");
            job.add_task(unit, "update");
            job.add_task(unit, "initialize", model_config);
            job.add_task(unit, "get_current_time");
            job.add_task(unit, "finalize");
        } else {
            const miaow::SimulationUnitHandle static_unit =
                job.add_unit("static", static_bmi);
            add_operation_pair(miaow::Task{static_unit, "run", model_config});

            // Exercise both plugins in-process and in their unit-owned subprocesses
            std::vector<caf::config_value::dictionary> configs(2, model_config);
            configs[0].insert("backend", "cpp");
            configs[0].insert("plugin_path", MIAOW_CPP_HEAT_PLUGIN_PATH);
            configs[1].insert("backend", "fortran");
            configs[1].insert("plugin_path", MIAOW_FORTRAN_HEAT_PLUGIN_PATH);
            for (const miaow::TaskExecutorId executor : {dynamic_bmi, subprocess_bmi}) {
                const std::string mode =
                    executor == dynamic_bmi ? "in_process_" : "subprocess_";
                for (std::size_t index = 0; index < configs.size(); ++index) {
                    const miaow::SimulationUnitHandle unit =
                        job.add_unit(mode + std::to_string(index), executor);
                    add_operation_pair(miaow::Task{unit, "run", configs[index]});
                }
            }
        }

        // Runtime saves the job record for the shared regression checker
        miaow::Runtime runtime(registry, runtime_options);
        const miaow::JobRecord record = runtime.submit_job(std::move(job)).get();
        miaow::print_job_summary(record);
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
