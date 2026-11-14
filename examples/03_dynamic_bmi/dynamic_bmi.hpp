#pragma once

/**
 * Build a dynamic-BMI job for C++ and Fortran heat models.
 */

#include <exception>
#include <string>
#include <vector>
#include <utility>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include <nlohmann/json.hpp>

namespace miaow::examples::dynamic_bmi {

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry,
                           const nlohmann::json& config) {
    // Resolve plugin paths and shared model settings from the optional config
    const std::string cpp_heat_plugin_path =
        config.value("cpp_plugin", MIAOW_CPP_HEAT_PLUGIN_PATH);
    const std::string fortran_heat_plugin_path =
        config.value("fortran_plugin", MIAOW_FORTRAN_HEAT_PLUGIN_PATH);

    // Select the shared initialization file and clock tolerance
    const std::string heat_config =
        config.value("init_config", "./data/configs/config_dynamic_heat.txt");
    const double tolerance_seconds =
        config.value("time_step_tolerance_seconds",
                     miaow::default_bmi_time_step_tolerance_seconds);

    // Register in-process and subprocess execution
    const miaow::TaskExecutorId dynamic_bmi = registry.add<miaow::DynamicBmiTaskExecutor>();
    const miaow::TaskExecutorId subprocess_bmi =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path());

    // Pair each plugin with the shared model settings
    std::vector<caf::config_value::dictionary> configs(2);
    configs[0].insert("backend", "cpp");
    configs[0].insert("plugin_path", cpp_heat_plugin_path);
    configs[1].insert("backend", "fortran");
    configs[1].insert("plugin_path", fortran_heat_plugin_path);
    for (auto& model_config : configs) {
        model_config.insert("init_config", heat_config);
        model_config.insert("time_step_tolerance_seconds", tolerance_seconds);
    }

    // Each configuration runs once in-process and once in its own subprocess
    // Each SimulationUnit owns its own executor instance
    miaow::Job job;
    for (const miaow::TaskExecutorId executor : {dynamic_bmi, subprocess_bmi}) {
        const std::string mode = executor == dynamic_bmi ? "in_process_" : "subprocess_";
        for (std::size_t index = 0; index < configs.size(); ++index) {
            const miaow::SimulationUnitId unit =
                job.add_unit(mode + std::to_string(index), executor);
            job.add_task(unit, "run", configs[index]);
        }
    }

    return job;
}

} // namespace miaow::examples::dynamic_bmi
