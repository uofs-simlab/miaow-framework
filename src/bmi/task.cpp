#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace miaow {

caf::config_value bmi_pipeline_config_to_value(const BmiPipelineConfig& pipeline_config) {
    if (pipeline_config.module_configs_.empty()) {
        throw std::runtime_error(
            "BMI pipeline config error: module_configs must contain at least one module");
    }
    if (pipeline_config.output_module_ >= pipeline_config.module_configs_.size()) {
        throw std::runtime_error(
            "BMI pipeline config error: output_module must refer to a configured module");
    }

    caf::config_value::dictionary config_dictionary;
    const std::string feature_id = io::trim(pipeline_config.feature_id_);
    if (!feature_id.empty()) {
        caf::config_value::dictionary feature;
        feature.insert("id", feature_id);
        config_dictionary.insert("feature", std::move(feature));
    }
    config_dictionary.insert(
        "global", bmi::make_bmi_multi_global_config(pipeline_config.module_configs_,
                                                    pipeline_config.forcing_file_,
                                                    pipeline_config.output_module_));
    return caf::config_value{std::move(config_dictionary)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const StaticBmiConfig& config) {
    if (io::trim(config.init_config_).empty()) {
        throw std::runtime_error(
            "BMI static config error: init_config must be a non-empty string");
    }

    BmiModuleConfig module_config("supplied_model", config.init_config_,
                                  config.variables_names_map_);
    module_config.output_variables_ = config.output_variables_;
    const BmiPipelineConfig pipeline_config(
        std::vector<BmiModuleConfig>{std::move(module_config)}, config.forcing_file_);
    return Task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config) {
    return bmi::make_dynamic_bmi_task(simulation_unit_id, config);
}

} // namespace miaow

namespace miaow::bmi {

Task make_dynamic_bmi_task(SimulationUnitId simulation_unit_id,
                           const DynamicBmiConfig& config, const std::string& feature_id,
                           TaskMetadata metadata) {
    if (io::trim(config.plugin_path_).empty()) {
        throw std::runtime_error(
            "BMI dynamic config error: plugin_path must be a non-empty string");
    }

    const std::string module_name =
        config.backend_ == DynamicBmiBackend::cpp ? "bmi_cpp" : "bmi_fortran";
    BmiModuleConfig module_config(module_name, config.backend_, config.plugin_path_,
                                  config.init_config_, config.variables_names_map_);
    module_config.output_variables_ = config.output_variables_;

    const BmiPipelineConfig pipeline_config(std::vector<BmiModuleConfig>{std::move(
                                                module_config)},
                                            config.forcing_file_, feature_id);
    Task task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config),
              std::move(metadata)};
    task.execution_mode_ = config.execution_mode_;
    task.subprocess_path_ = config.subprocess_path_;
    return task;
}

} // namespace miaow::bmi
