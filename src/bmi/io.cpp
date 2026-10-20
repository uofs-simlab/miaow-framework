#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace miaow {

using nlohmann::json;

namespace {

// Validate public task values before serializing the internal pipeline config
void validate_time_step_tolerance_seconds(double tolerance_seconds,
                                          std::string_view error_prefix) {
    if (!std::isfinite(tolerance_seconds) || tolerance_seconds < 0.0) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "time_step_tolerance_seconds must be finite and "
                                 "non-negative");
    }
}

void validate_data_format(BmiDataFormat format, std::string_view error_prefix,
                          std::string_view context) {
    if (format != BmiDataFormat::csv) {
        throw std::runtime_error(std::string(error_prefix) + "unsupported " +
                                 std::string(context) + " format");
    }
}

void validate_output_interval(const std::optional<double>& interval,
                              std::string_view error_prefix) {
    if (interval && (!std::isfinite(*interval) || *interval <= 0.0)) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "output interval must be finite and positive");
    }
}

// Normalized task IO shared by static and dynamic BMI configurations
struct BmiTaskIoSettings {
    BmiDataFormat forcing_format_{BmiDataFormat::csv};
    std::string forcing_file_;
    std::optional<std::string> forcing_feature_id_variable_;
    std::map<std::string, std::string> variables_names_map_;
    std::optional<std::vector<std::string>> output_variables_;
    std::optional<double> output_interval_;
};

// Validate and collect format-independent task IO settings
template <class BmiConfig>
BmiTaskIoSettings normalize_bmi_task_io(const BmiConfig& config,
                                        std::string_view error_prefix) {
    BmiTaskIoSettings settings;
    if (config.forcing_) {
        const BmiForcingConfig& forcing = *config.forcing_;
        if (forcing.format_ != BmiDataFormat::csv &&
            forcing.format_ != BmiDataFormat::netcdf) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "unsupported forcing format");
        }
        if (forcing.format_ == BmiDataFormat::csv && forcing.feature_id_variable_) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "CSV forcing does not accept feature_id_variable");
        }
        if (forcing.format_ == BmiDataFormat::netcdf &&
            (!forcing.feature_id_variable_ ||
             io::trim(*forcing.feature_id_variable_).empty())) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "NetCDF forcing requires a non-empty "
                                     "feature_id_variable");
        }
        settings.forcing_format_ = config.forcing_->format_;
        settings.forcing_file_ = io::trim(config.forcing_->file_);
        if (settings.forcing_file_.empty()) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "forcing file must be a non-empty string");
        }
        if (config.forcing_->feature_id_variable_) {
            settings.forcing_feature_id_variable_ =
                io::trim(*config.forcing_->feature_id_variable_);
        }
        settings.variables_names_map_ = config.forcing_->variables_names_map_;
    }

    if (config.output_) {
        validate_data_format(config.output_->format_, error_prefix, "output");
        validate_output_interval(config.output_->interval_, error_prefix);
        settings.output_variables_ = config.output_->variables_;
        settings.output_interval_ = config.output_->interval_;
    }
    return settings;
}

// Return the validated pipeline config value carried by one task
const caf::config_value& bmi_pipeline_config_value(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error("BMI pipeline config error: task must contain exactly one "
                                 "BmiPipelineConfig argument");
    }

    const caf::config_value& pipeline_config_value = task.args_.front();
    if (caf::get_if<caf::config_value::dictionary>(&pipeline_config_value) == nullptr) {
        throw std::runtime_error(
            "BMI pipeline config error: first task argument must be a BmiPipelineConfig");
    }
    return pipeline_config_value;
}

// Convert CAF task argument values into standard JSON values
json config_value_to_json(const caf::config_value& value) {
    if (const caf::config_value::dictionary* dictionary =
            caf::get_if<caf::config_value::dictionary>(&value)) {
        json::object_t object;
        for (const std::pair<const std::string, caf::config_value>& entry : *dictionary) {
            object.emplace(entry.first, config_value_to_json(entry.second));
        }
        return object;
    }
    if (const caf::config_value::list* list =
            caf::get_if<caf::config_value::list>(&value)) {
        json::array_t array;
        array.reserve(list->size());
        for (const caf::config_value& list_value : *list) {
            array.emplace_back(config_value_to_json(list_value));
        }
        return array;
    }
    if (const std::string* string_value = caf::get_if<std::string>(&value)) {
        return *string_value;
    }
    if (const caf::config_value::integer* integer_value =
            caf::get_if<caf::config_value::integer>(&value)) {
        return *integer_value;
    }
    if (const caf::config_value::real* real_value =
            caf::get_if<caf::config_value::real>(&value)) {
        return *real_value;
    }
    if (const caf::config_value::boolean* boolean_value =
            caf::get_if<caf::config_value::boolean>(&value)) {
        return *boolean_value;
    }
    return nullptr;
}

} // namespace

BmiModuleConfig::BmiModuleConfig(std::string name, std::string config_file,
                                 std::map<std::string, std::string> variables_names_map)
    : name_(std::move(name)), config_file_(std::move(config_file)),
      variables_names_map_(std::move(variables_names_map)) {}

BmiModuleConfig::BmiModuleConfig(std::string name, DynamicBmiBackend backend,
                                 std::string plugin_path, std::string config_file,
                                 std::map<std::string, std::string> variables_names_map)
    : name_(std::move(name)), backend_(backend),
      plugin_path_(std::move(plugin_path)), config_file_(std::move(config_file)),
      variables_names_map_(std::move(variables_names_map)) {}

// Decode one pipeline module from its JSON representation
BmiModuleConfig::BmiModuleConfig(const json::object_t& module_object,
                                 std::string_view error_prefix)
    : name_(io::trim(module_object.at("name").get<std::string>())) {
    const auto& module_params = module_object.at("params").get_ref<const json::object_t&>();
    config_file_ = io::trim(module_params.at("init_config").get<std::string>());

    if (const auto map_value = module_params.find("variables_names_map");
        map_value != module_params.end()) {
        const auto& map_object = map_value->second.get_ref<const json::object_t&>();
        for (const auto& [key, value] : map_object) {
            const std::string map_key = io::trim(key);
            if (map_key.empty() || !value.is_string()) {
                io::throw_config_error(
                    error_prefix,
                    "variables_names_map entries must map non-empty strings to strings");
            }

            const std::string map_value_text = io::trim(value.get<std::string>());
            if (map_value_text.empty()) {
                io::throw_config_error(
                    error_prefix,
                    "variables_names_map entries must map to non-empty strings");
            }
            variables_names_map_.emplace(map_key, map_value_text);
        }
    }

    // CSV output variables preserve the order declared by the module config
    if (const auto output_value = module_params.find("output_variables");
        output_value != module_params.end()) {
        if (!output_value->second.is_array()) {
            io::throw_config_error(error_prefix,
                                   "output_variables must be an array of strings");
        }
        output_variables_.emplace();
        for (const json& output_variable_value :
             output_value->second.get_ref<const json::array_t&>()) {
            if (!output_variable_value.is_string()) {
                io::throw_config_error(
                    error_prefix, "output_variables entries must be non-empty strings");
            }
            const std::string output_variable =
                io::trim(output_variable_value.get<std::string>());
            if (output_variable.empty()) {
                io::throw_config_error(
                    error_prefix, "output_variables entries must be non-empty strings");
            }
            output_variables_->push_back(output_variable);
        }
    }

    // Supplied model modules are bound to a statically typed simulation unit
    if (name_ == "supplied_model") {
        return;
    }

    // Dynamic modules require a plugin library path
    plugin_path_ = io::trim(module_params.at("library_file").get<std::string>());

    if (name_ == "bmi_fortran") {
        backend_ = DynamicBmiBackend::fortran;
        return;
    }

    if (name_.starts_with("bmi_cpp")) {
        backend_ = DynamicBmiBackend::cpp;
        cpp_create_function_ = "bmi_model_create";
        cpp_destroy_function_ = "bmi_model_destroy";
        if (const auto create_function = module_params.find("cpp_create_function");
            create_function != module_params.end()) {
            const std::string parsed =
                io::trim(create_function->second.get<std::string>());
            if (!parsed.empty()) {
                cpp_create_function_ = parsed;
            }
        }
        if (const auto destroy_function = module_params.find("cpp_destroy_function");
            destroy_function != module_params.end()) {
            const std::string parsed =
                io::trim(destroy_function->second.get<std::string>());
            if (!parsed.empty()) {
                cpp_destroy_function_ = parsed;
            }
        }
        return;
    }

    io::throw_config_error(error_prefix, "unsupported module '" + name_ + "'");
}

BmiPipelineConfig::BmiPipelineConfig(std::vector<BmiModuleConfig> module_configs,
                                     std::string forcing_file, std::string feature_id,
                                     std::optional<double> output_interval,
                                     double time_step_tolerance_seconds,
                                     BmiDataFormat forcing_format,
                                     std::optional<std::string> forcing_feature_id_variable)
    : module_configs_(std::move(module_configs)), forcing_format_(forcing_format),
      forcing_file_(std::move(forcing_file)),
      forcing_feature_id_variable_(std::move(forcing_feature_id_variable)),
      feature_id_(std::move(feature_id)), output_interval_(output_interval),
      time_step_tolerance_seconds_(time_step_tolerance_seconds) {
    if (module_configs_.empty()) {
        throw std::runtime_error(
            "BMI pipeline config error: module_configs must contain at least one module");
    }
}

// Decode a pipeline config from its JSON representation
BmiPipelineConfig::BmiPipelineConfig(const json::object_t& pipeline_config,
                                     std::string_view error_prefix) {
    const auto& global = pipeline_config.at("global").get_ref<const json::object_t&>();
    const auto& formulations = global.at("formulations").get_ref<const json::array_t&>();
    if (formulations.size() != 1) {
        io::throw_config_error(error_prefix, "formulations must contain one entry");
    }

    const auto& formulation = formulations.at(0).get_ref<const json::object_t&>();
    const std::string formulation_name =
        io::trim(formulation.at("name").get<std::string>());
    if (formulation_name != "bmi_multi") {
        io::throw_config_error(error_prefix,
                               "unsupported formulation '" + formulation_name + "'");
    }

    const auto& bmi_multi_params =
        formulation.at("params").get_ref<const json::object_t&>();
    const auto& modules = bmi_multi_params.at("modules").get_ref<const json::array_t&>();
    module_configs_.reserve(modules.size());
    for (const json& module_value : modules) {
        module_configs_.emplace_back(module_value.get_ref<const json::object_t&>(),
                                     error_prefix);
    }
    if (module_configs_.empty()) {
        io::throw_config_error(error_prefix,
                               "module_configs must contain at least one module");
    }

    // Check the module index before converting; JSON integer conversion can truncate.
    const double output_module = bmi_multi_params.at("output_module").get<double>();
    if (output_module < 0.0 || output_module >= module_configs_.size() ||
        std::trunc(output_module) != output_module) {
        io::throw_config_error(error_prefix,
                               "output_module must refer to a configured module");
    }
    output_module_ = static_cast<std::size_t>(output_module);
    if (const auto output_interval = bmi_multi_params.find("output_interval");
        output_interval != bmi_multi_params.end()) {
        output_interval_ = output_interval->second.get<double>();
    }
    time_step_tolerance_seconds_ =
        bmi_multi_params.at("time_step_tolerance_seconds").get<double>();

    if (const auto forcing_value = global.find("forcing");
        forcing_value != global.end()) {
        const auto& forcing = forcing_value->second.get_ref<const json::object_t&>();
        const std::string provider =
            io::trim(forcing.at("provider").get<std::string>());
        if (provider == "CsvFile") {
            forcing_format_ = BmiDataFormat::csv;
        } else if (provider == "NetCdf") {
            forcing_format_ = BmiDataFormat::netcdf;
        } else {
            io::throw_config_error(error_prefix,
                                   "unsupported forcing provider '" + provider + "'");
        }
        forcing_file_ = io::trim(forcing.at("file").get<std::string>());
        if (const auto feature_id_variable = forcing.find("feature_id_variable");
            feature_id_variable != forcing.end()) {
            forcing_feature_id_variable_ =
                io::trim(feature_id_variable->second.get<std::string>());
        }
    }

    if (const auto feature_value = pipeline_config.find("feature");
        feature_value != pipeline_config.end()) {
        const auto& feature = feature_value->second.get_ref<const json::object_t&>();
        feature_id_ = io::trim(feature.at("id").get<std::string>());
    }
}

// Decode the internal pipeline config carried by a task
BmiPipelineConfig::BmiPipelineConfig(const Task& task)
    : BmiPipelineConfig(
          config_value_to_json(bmi_pipeline_config_value(task))
              .get_ref<const json::object_t&>(),
          "BMI pipeline config error: ") {}

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
    config_dictionary.insert("global", bmi::make_bmi_multi_global_config(
                                           pipeline_config.module_configs_,
                                           pipeline_config.forcing_file_,
                                           pipeline_config.output_module_,
                                           pipeline_config.output_interval_,
                                           pipeline_config.time_step_tolerance_seconds_,
                                           pipeline_config.forcing_format_,
                                           pipeline_config.forcing_feature_id_variable_));
    return caf::config_value{std::move(config_dictionary)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const StaticBmiConfig& config) {
    return bmi::make_bmi_task(simulation_unit_id, config);
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config) {
    return bmi::make_bmi_task(simulation_unit_id, config);
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config,
                   const std::string& feature_id, TaskMetadata metadata) {
    return bmi::make_bmi_task(simulation_unit_id, config, feature_id, std::move(metadata));
}

} // namespace miaow

namespace miaow::bmi {

Task make_bmi_task(SimulationUnitId simulation_unit_id, const StaticBmiConfig& config) {
    // Validate required static BMI settings
    constexpr std::string_view error_prefix = "BMI static config error: ";
    if (io::trim(config.init_config_).empty()) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "init_config must be a non-empty string");
    }
    validate_time_step_tolerance_seconds(config.time_step_tolerance_seconds_, error_prefix);

    BmiTaskIoSettings io_settings = normalize_bmi_task_io(config, error_prefix);
    if (io_settings.forcing_format_ == BmiDataFormat::netcdf) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "NetCDF forcing requires a dynamic task feature id");
    }

    // Build the BMI module config for the supplied model
    BmiModuleConfig module_config("supplied_model", config.init_config_,
                                  std::move(io_settings.variables_names_map_));
    module_config.output_variables_ = std::move(io_settings.output_variables_);

    // Build the pipeline task for the registered static model
    const BmiPipelineConfig pipeline_config(
        std::vector<BmiModuleConfig>{std::move(module_config)},
        std::move(io_settings.forcing_file_), "", io_settings.output_interval_,
        config.time_step_tolerance_seconds_, io_settings.forcing_format_,
        std::move(io_settings.forcing_feature_id_variable_));
    return Task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config,
                   const std::string& feature_id, TaskMetadata metadata) {
    // Validate required dynamic BMI settings
    constexpr std::string_view error_prefix = "BMI dynamic config error: ";
    if (io::trim(config.plugin_path_).empty()) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "plugin_path must be a non-empty string");
    }
    validate_time_step_tolerance_seconds(config.time_step_tolerance_seconds_, error_prefix);

    BmiTaskIoSettings io_settings = normalize_bmi_task_io(config, error_prefix);
    const std::string normalized_feature_id = io::trim(feature_id);
    if (io_settings.forcing_format_ == BmiDataFormat::netcdf &&
        normalized_feature_id.empty()) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "NetCDF forcing requires a non-empty feature id");
    }

    // Build the backend-specific BMI module config
    const std::string module_name =
        config.backend_ == DynamicBmiBackend::cpp ? "bmi_cpp" : "bmi_fortran";
    BmiModuleConfig module_config(module_name, config.backend_, config.plugin_path_,
                                  config.init_config_,
                                  std::move(io_settings.variables_names_map_));
    module_config.output_variables_ = std::move(io_settings.output_variables_);

    // Build the pipeline task with its dynamic model configuration
    const BmiPipelineConfig pipeline_config(
        std::vector<BmiModuleConfig>{std::move(module_config)},
        std::move(io_settings.forcing_file_), normalized_feature_id,
        io_settings.output_interval_, config.time_step_tolerance_seconds_,
        io_settings.forcing_format_, std::move(io_settings.forcing_feature_id_variable_));
    return Task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config),
                std::move(metadata)};
}

namespace {

// Insert explicit forcing variable names into module params
void add_variables_names_map_to_params(
    caf::config_value::dictionary& module_params,
    const std::map<std::string, std::string>& variables_names_map) {
    if (variables_names_map.empty()) {
        return;
    }

    // Store variable maps only when the pipeline config needs explicit forcing names
    caf::config_value::dictionary map_value;
    for (const std::pair<const std::string, std::string>& map_entry : variables_names_map) {
        if (io::trim(map_entry.first).empty()) {
            throw std::runtime_error(
                "BMI pipeline config error: variables_names_map contains an empty "
                "BMI input variable name");
        }
        if (io::trim(map_entry.second).empty()) {
            throw std::runtime_error(
                "BMI pipeline config error: variables_names_map entry '" + map_entry.first +
                "' must be a non-empty string");
        }
        map_value.insert(map_entry.first, map_entry.second);
    }
    module_params.insert("variables_names_map", std::move(map_value));
}

// Write output values for one BMI variable into the current CSV row
template <typename T>
void write_var_values(::bmi::Bmi& model, const std::string& name, std::size_t n_items,
                      std::ofstream& output_stream) {
    std::vector<T> buffer(n_items);
    model.GetValue(name, buffer.data());
    for (const T& value : buffer) {
        output_stream << ",";
        if constexpr (std::is_floating_point_v<T>) {
            output_stream << std::setprecision(std::numeric_limits<T>::max_digits10);
        }
        output_stream << value;
    }
}

} // namespace

// Build the serialized bmi_multi global block for one pipeline config
caf::config_value::dictionary make_bmi_multi_global_config(
    const std::vector<BmiModuleConfig>& module_configs, const std::string& forcing_file,
    std::size_t output_module, std::optional<double> output_interval,
    double time_step_tolerance_seconds, BmiDataFormat forcing_format,
    std::optional<std::string> feature_id_variable) {
    caf::config_value::list modules;
    for (const BmiModuleConfig& module_config : module_configs) {
        caf::config_value::dictionary module_params;
        if (module_config.backend_) {
            module_params.insert("library_file", module_config.plugin_path_);
        }
        module_params.insert("init_config", module_config.config_file_);
        if (module_config.cpp_create_function_) {
            module_params.insert("cpp_create_function",
                                 *module_config.cpp_create_function_);
        }
        if (module_config.cpp_destroy_function_) {
            module_params.insert("cpp_destroy_function",
                                 *module_config.cpp_destroy_function_);
        }
        add_variables_names_map_to_params(module_params,
                                          module_config.variables_names_map_);

        // Store an empty list when all BMI outputs are selected
        if (module_config.output_variables_) {
            caf::config_value::list output_variables;
            output_variables.reserve(module_config.output_variables_->size());
            for (const std::string& output_variable : *module_config.output_variables_) {
                output_variables.emplace_back(output_variable);
            }
            module_params.insert("output_variables", std::move(output_variables));
        }

        caf::config_value::dictionary module_value;
        module_value.insert("name", module_config.name_);
        module_value.insert("params", std::move(module_params));
        modules.emplace_back(std::move(module_value));
    }

    caf::config_value::dictionary formulation_params;
    formulation_params.insert("output_module",
                              caf::config_value::integer{
                                  static_cast<caf::config_value::integer>(output_module)});
    if (output_interval) {
        formulation_params.insert(
            "output_interval",
            caf::config_value::real{
                static_cast<caf::config_value::real>(*output_interval)});
    }
    formulation_params.insert(
        "time_step_tolerance_seconds",
        caf::config_value::real{
            static_cast<caf::config_value::real>(time_step_tolerance_seconds)});
    formulation_params.insert("modules", std::move(modules));

    caf::config_value::dictionary formulation;
    formulation.insert("name", "bmi_multi");
    formulation.insert("params", std::move(formulation_params));

    caf::config_value::list formulations;
    formulations.emplace_back(std::move(formulation));

    caf::config_value::dictionary global;
    global.insert("formulations", std::move(formulations));
    if (!io::trim(forcing_file).empty()) {
        caf::config_value::dictionary forcing;
        forcing.insert("provider",
                       forcing_format == BmiDataFormat::netcdf ? "NetCdf" : "CsvFile");
        forcing.insert("file", forcing_file);
        if (feature_id_variable) {
            forcing.insert("feature_id_variable", *feature_id_variable);
        }
        global.insert("forcing", std::move(forcing));
    }

    return global;
}

// Write and report the resolved pipeline config when artifact storage is enabled
std::optional<std::string> write_bmi_pipeline_config_artifact(const Task& task) {
    if (task.task_output_dir_.empty()) {
        return std::nullopt;
    }

    const std::string output_name = task.task_name_.empty() ? "task" : task.task_name_;
    const std::string artifact_path = output_name + ".json";
    const std::filesystem::path results_directory =
        std::filesystem::absolute(task.task_output_dir_).lexically_normal();
    const std::filesystem::path job_directory = results_directory.parent_path();
    std::filesystem::create_directories(job_directory);
    const std::filesystem::path config_path = job_directory / artifact_path;

    std::ofstream output(config_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open BMI pipeline config file '" +
                                 config_path.string() + "'");
    }
    output << config_value_to_json(bmi_pipeline_config_value(task)).dump() << '\n';
    return artifact_path;
}

// Store the task-relative path once for every concrete writer
OutputWriter::OutputWriter(std::string artifact_path)
    : artifact_path_(std::move(artifact_path)) {}

// Expose the task-relative artifact path without format dispatch
const std::string& OutputWriter::artifact_path() const {
    return artifact_path_;
}

// Validate the output selection before opening its CSV artifact
CsvOutputWriter::CsvOutputWriter(
    ::bmi::Bmi& model, const std::string& output_dir, const std::string& output_name,
    const std::vector<std::string>& output_variables)
    : OutputWriter((std::filesystem::path(output_dir).filename() /
                    (output_name + ".csv"))
                       .string()) {
    if (output_name.empty()) {
        throw std::runtime_error("BMI output error: task output name is empty");
    }

    const std::vector<std::string> model_var_names = model.GetOutputVarNames();
    variables_.reserve(output_variables.size());

    // Configured CSV output variables must exist on the exposed BMI model
    for (const std::string& name : output_variables) {
        const std::vector<std::string>::const_iterator it =
            std::find(model_var_names.begin(), model_var_names.end(), name);
        if (it == model_var_names.end()) {
            throw std::runtime_error("BMI output error: output variable '" + name +
                                     "' not found");
        }
        variables_.emplace_back(model, name, "BMI output error: ");
    }

    // Open the artifact only after every selected variable has been validated
    const std::filesystem::path output_path =
        std::filesystem::path(output_dir) / (output_name + ".csv");
    stream_.open(output_path);
    if (!stream_.is_open()) {
        throw std::runtime_error("BMI output error: unable to open output file '" +
                                 output_path.string() + "'");
    }

    // Write columns in the same flattened variable order used for every data row
    stream_ << "time";
    for (const BmiVariable& variable : variables_) {
        if (variable.item_count() == 1) {
            stream_ << "," << variable.name();
            continue;
        }

        for (std::size_t item_index = 0; item_index < variable.item_count(); ++item_index) {
            stream_ << "," << variable.name() << "_" << item_index;
        }
    }
    stream_ << "\n";
}

void CsvOutputWriter::write_step(::bmi::Bmi& model, double time) {
    // Write one complete model state using cached variable metadata
    stream_ << std::setprecision(std::numeric_limits<double>::max_digits10) << time;
    for (const BmiVariable& variable : variables_) {
        switch (variable.type()) {
        case BmiValueType::bmi_double:
            write_var_values<double>(model, variable.name(), variable.item_count(), stream_);
            break;
        case BmiValueType::bmi_float:
            write_var_values<float>(model, variable.name(), variable.item_count(), stream_);
            break;
        case BmiValueType::bmi_int:
            write_var_values<int>(model, variable.name(), variable.item_count(), stream_);
            break;
        }
    }
    stream_ << "\n";
}

} // namespace miaow::bmi
