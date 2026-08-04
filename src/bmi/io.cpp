#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/json.hpp>

namespace miaow {

namespace {

// Validate public task values before serializing the internal pipeline config
void validate_time_step_tolerance(double tolerance, std::string_view error_prefix) {
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "time_step_tolerance must be finite and non-negative");
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

BmiPipelineConfig::BmiPipelineConfig(std::vector<BmiModuleConfig> module_configs,
                                     std::string forcing_file, std::string feature_id,
                                     std::optional<double> output_interval,
                                     double time_step_tolerance)
    : module_configs_(std::move(module_configs)),
      forcing_file_(std::move(forcing_file)), feature_id_(std::move(feature_id)),
      output_interval_(output_interval), time_step_tolerance_(time_step_tolerance) {
    if (module_configs_.empty()) {
        throw std::runtime_error(
            "BMI pipeline config error: module_configs must contain at least one module");
    }
}

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
                                                    pipeline_config.output_module_,
                                                    pipeline_config.output_interval_,
                                                    pipeline_config.time_step_tolerance_));
    return caf::config_value{std::move(config_dictionary)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const StaticBmiConfig& config) {
    return bmi::make_bmi_task(simulation_unit_id, config);
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config) {
    return bmi::make_bmi_task(simulation_unit_id, config);
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
    validate_time_step_tolerance(config.time_step_tolerance_, error_prefix);

    // Validate and collect optional forcing settings
    std::string forcing_file;
    std::map<std::string, std::string> variables_names_map;
    if (config.forcing_) {
        validate_data_format(config.forcing_->format_, error_prefix, "forcing");
        forcing_file = io::trim(config.forcing_->file_);
        if (forcing_file.empty()) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "forcing file must be a non-empty string");
        }
        variables_names_map = config.forcing_->variables_names_map_;
    }

    // Validate and collect optional output settings
    std::optional<std::vector<std::string>> output_variables;
    std::optional<double> output_interval;
    if (config.output_) {
        validate_data_format(config.output_->format_, error_prefix, "output");
        validate_output_interval(config.output_->interval_, error_prefix);
        output_variables = config.output_->variables_;
        output_interval = config.output_->interval_;
    }

    // Build the BMI module config for the supplied model
    BmiModuleConfig module_config("supplied_model", config.init_config_,
                                  std::move(variables_names_map));
    module_config.output_variables_ = std::move(output_variables);

    // Build the pipeline task for the registered static model
    const BmiPipelineConfig pipeline_config(
        std::vector<BmiModuleConfig>{std::move(module_config)}, std::move(forcing_file), "",
        output_interval, config.time_step_tolerance_);
    return Task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config)};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const DynamicBmiConfig& config,
                   const std::string& feature_id, TaskMetadata metadata) {
    // Validate required dynamic BMI settings
    constexpr std::string_view error_prefix = "BMI dynamic config error: ";
    if (io::trim(config.plugin_path_).empty()) {
        throw std::runtime_error(std::string(error_prefix) +
                                 "plugin_path must be a non-empty string");
    }
    validate_time_step_tolerance(config.time_step_tolerance_, error_prefix);

    // Validate and collect optional forcing settings
    std::string forcing_file;
    std::map<std::string, std::string> variables_names_map;
    if (config.forcing_) {
        validate_data_format(config.forcing_->format_, error_prefix, "forcing");
        forcing_file = io::trim(config.forcing_->file_);
        if (forcing_file.empty()) {
            throw std::runtime_error(std::string(error_prefix) +
                                     "forcing file must be a non-empty string");
        }
        variables_names_map = config.forcing_->variables_names_map_;
    }

    // Validate and collect optional output settings
    std::optional<std::vector<std::string>> output_variables;
    std::optional<double> output_interval;
    if (config.output_) {
        validate_data_format(config.output_->format_, error_prefix, "output");
        validate_output_interval(config.output_->interval_, error_prefix);
        output_variables = config.output_->variables_;
        output_interval = config.output_->interval_;
    }

    // Build the backend-specific BMI module config
    const std::string module_name =
        config.backend_ == DynamicBmiBackend::cpp ? "bmi_cpp" : "bmi_fortran";
    BmiModuleConfig module_config(module_name, config.backend_, config.plugin_path_,
                                  config.init_config_, std::move(variables_names_map));
    module_config.output_variables_ = std::move(output_variables);

    // Build the pipeline task with its requested execution settings
    const BmiPipelineConfig pipeline_config(
        std::vector<BmiModuleConfig>{std::move(module_config)}, std::move(forcing_file),
        feature_id, output_interval, config.time_step_tolerance_);
    Task task{simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config),
              std::move(metadata)};
    task.execution_mode_ = config.execution_mode_;
    task.subprocess_path_ = config.subprocess_path_;
    return task;
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
boost::json::value config_value_to_json(const caf::config_value& value) {
    if (const caf::config_value::dictionary* dictionary =
            caf::get_if<caf::config_value::dictionary>(&value)) {
        boost::json::object object;
        for (const std::pair<const std::string, caf::config_value>& entry : *dictionary) {
            object.emplace(entry.first, config_value_to_json(entry.second));
        }
        return object;
    }
    if (const caf::config_value::list* list =
            caf::get_if<caf::config_value::list>(&value)) {
        boost::json::array array;
        array.reserve(list->size());
        for (const caf::config_value& list_value : *list) {
            array.emplace_back(config_value_to_json(list_value));
        }
        return array;
    }
    if (const std::string* string_value = caf::get_if<std::string>(&value)) {
        return boost::json::value_from(*string_value);
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

BmiPipelineConfig parse_bmi_pipeline_config(const boost::json::object& pipeline_config,
                                            std::string_view error_prefix) {
    const boost::json::object& global = pipeline_config.at("global").as_object();
    const boost::json::array& formulations = global.at("formulations").as_array();
    if (formulations.size() != 1) {
        io::throw_config_error(error_prefix, "formulations must contain one entry");
    }

    const boost::json::object& formulation = formulations.at(0).as_object();
    const std::string formulation_name =
        io::trim(std::string(formulation.at("name").as_string()));
    if (formulation_name != "bmi_multi") {
        io::throw_config_error(error_prefix,
                               "unsupported formulation '" + formulation_name + "'");
    }

    const boost::json::object& bmi_multi_params = formulation.at("params").as_object();
    const boost::json::array& modules = bmi_multi_params.at("modules").as_array();
    std::vector<BmiModuleConfig> module_configs;
    module_configs.reserve(modules.size());
    for (const boost::json::value& module_value : modules) {
        module_configs.push_back(
            parse_bmi_module_config(module_value.as_object(), error_prefix));
    }

    BmiPipelineConfig parsed_config{std::move(module_configs)};
    parsed_config.output_module_ =
        bmi_multi_params.at("output_module").to_number<std::size_t>();
    if (parsed_config.output_module_ >= parsed_config.module_configs_.size()) {
        io::throw_config_error(error_prefix,
                               "output_module must refer to a configured module");
    }
    if (const boost::json::value* output_interval =
            bmi_multi_params.if_contains("output_interval")) {
        parsed_config.output_interval_ = output_interval->to_number<double>();
    }
    parsed_config.time_step_tolerance_ =
        bmi_multi_params.at("time_step_tolerance").to_number<double>();

    if (const boost::json::value* forcing_value = global.if_contains("forcing")) {
        const boost::json::object& forcing = forcing_value->as_object();
        const std::string provider =
            io::trim(std::string(forcing.at("provider").as_string()));
        if (provider != "CsvFile") {
            io::throw_config_error(error_prefix,
                                   "unsupported forcing provider '" + provider + "'");
        }
        parsed_config.forcing_file_ = io::trim(std::string(forcing.at("file").as_string()));
    }

    if (const boost::json::value* feature_value = pipeline_config.if_contains("feature")) {
        const boost::json::object& feature = feature_value->as_object();
        parsed_config.feature_id_ = io::trim(std::string(feature.at("id").as_string()));
    }

    return parsed_config;
}

} // namespace

BmiModuleConfig parse_bmi_module_config(const boost::json::object& module_object,
                                        std::string_view error_prefix) {
    std::string module_name = io::trim(std::string(module_object.at("name").as_string()));

    const boost::json::object& module_params = module_object.at("params").as_object();
    std::string config_file =
        io::trim(std::string(module_params.at("init_config").as_string()));

    std::map<std::string, std::string> variables_names_map;
    if (const boost::json::value* map_value =
            module_params.if_contains("variables_names_map")) {
        const boost::json::object& map_object = map_value->as_object();
        for (const boost::json::key_value_pair& map_entry : map_object) {
            const std::string map_key = io::trim(std::string(map_entry.key()));
            if (map_key.empty() || !map_entry.value().is_string()) {
                io::throw_config_error(
                    error_prefix,
                    "variables_names_map entries must map non-empty strings to strings");
            }

            const std::string map_value_text =
                io::trim(std::string(map_entry.value().as_string()));
            if (map_value_text.empty()) {
                io::throw_config_error(
                    error_prefix,
                    "variables_names_map entries must map to non-empty strings");
            }
            variables_names_map.emplace(map_key, map_value_text);
        }
    }

    // CSV output variables preserve the order declared by the module config
    std::optional<std::vector<std::string>> output_variables;
    if (const boost::json::value* output_value =
            module_params.if_contains("output_variables")) {
        if (!output_value->is_array()) {
            io::throw_config_error(error_prefix,
                                   "output_variables must be an array of strings");
        }
        output_variables.emplace();
        for (const boost::json::value& output_variable_value : output_value->as_array()) {
            if (!output_variable_value.is_string()) {
                io::throw_config_error(
                    error_prefix, "output_variables entries must be non-empty strings");
            }
            const std::string output_variable =
                io::trim(std::string(output_variable_value.as_string()));
            if (output_variable.empty()) {
                io::throw_config_error(
                    error_prefix, "output_variables entries must be non-empty strings");
            }
            output_variables->push_back(output_variable);
        }
    }

    // Supplied model modules are bound to a statically typed simulation unit
    if (module_name == "supplied_model") {
        BmiModuleConfig module_config{std::move(module_name), std::move(config_file),
                                      std::move(variables_names_map)};
        module_config.output_variables_ = std::move(output_variables);
        return module_config;
    }

    // Otherwise, it's a dynamic module that requires a plugin library path
    std::string plugin_path =
        io::trim(std::string(module_params.at("library_file").as_string()));

    // Fortran backend
    if (module_name == "bmi_fortran") {
        BmiModuleConfig module_config{std::move(module_name), DynamicBmiBackend::fortran,
                                      std::move(plugin_path), std::move(config_file),
                                      std::move(variables_names_map)};
        module_config.output_variables_ = std::move(output_variables);
        return module_config;
    }

    // C++ backend
    if (module_name.starts_with("bmi_cpp")) {
        BmiModuleConfig module_config{std::move(module_name), DynamicBmiBackend::cpp,
                                      std::move(plugin_path), std::move(config_file),
                                      std::move(variables_names_map)};
        module_config.output_variables_ = std::move(output_variables);
        module_config.cpp_create_function_ = "bmi_model_create";
        module_config.cpp_destroy_function_ = "bmi_model_destroy";
        if (const boost::json::value* create_function =
                module_params.if_contains("cpp_create_function")) {
            const std::string parsed = io::trim(std::string(create_function->as_string()));
            if (!parsed.empty()) {
                module_config.cpp_create_function_ = parsed;
            }
        }
        if (const boost::json::value* destroy_function =
                module_params.if_contains("cpp_destroy_function")) {
            const std::string parsed = io::trim(std::string(destroy_function->as_string()));
            if (!parsed.empty()) {
                module_config.cpp_destroy_function_ = parsed;
            }
        }
        return module_config;
    }

    io::throw_config_error(error_prefix, "unsupported module '" + module_name + "'");
}

// Build the serialized bmi_multi global block for one pipeline config
caf::config_value::dictionary
make_bmi_multi_global_config(const std::vector<BmiModuleConfig>& module_configs,
                             const std::string& forcing_file, std::size_t output_module,
                             std::optional<double> output_interval,
                             double time_step_tolerance) {
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
        "time_step_tolerance",
        caf::config_value::real{
            static_cast<caf::config_value::real>(time_step_tolerance)});
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
        forcing.insert("provider", "CsvFile");
        forcing.insert("file", forcing_file);
        global.insert("forcing", std::move(forcing));
    }

    return global;
}

// Decode the internal pipeline config carried by a task
BmiPipelineConfig bmi_pipeline_config_from_task(const Task& task) {
    const boost::json::value root =
        config_value_to_json(bmi_pipeline_config_value(task));
    return parse_bmi_pipeline_config(root.as_object(), "BMI pipeline config error: ");
}

// Read the BMI pipeline config JSON consumed by the BMI pipeline
BmiPipelineConfig read_bmi_pipeline_config_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("unable to open BMI pipeline config file '" +
                                 path.string() + "'");
    }
    const boost::json::value root = boost::json::parse(input);
    return parse_bmi_pipeline_config(root.as_object(), "BMI pipeline config error: ");
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
    output << boost::json::serialize(
                  config_value_to_json(bmi_pipeline_config_value(task)))
           << "\n";
    return artifact_path;
}

// Open one CSV file for the selected BMI output variables
OutputFile open_output_file(::bmi::Bmi& model, const std::string& output_dir,
                            const std::string& output_name,
                            const std::vector<std::string>& output_variables) {
    if (output_name.empty()) {
        throw std::runtime_error("BMI output error: task output name is empty");
    }

    const std::filesystem::path output_directory(output_dir);
    const std::filesystem::path output_filename = output_name + ".csv";
    const std::filesystem::path output_path = output_directory / output_filename;

    const std::vector<std::string> model_var_names = model.GetOutputVarNames();

    // Configured CSV output variables must exist on the exposed BMI model
    for (const std::string& name : output_variables) {
        const std::vector<std::string>::const_iterator it =
            std::find(model_var_names.begin(), model_var_names.end(), name);
        if (it == model_var_names.end()) {
            throw std::runtime_error("BMI output error: output variable '" + name +
                                     "' not found");
        }
    }

    OutputFile output_file;
    output_file.artifact_path_ = (output_directory.filename() / output_filename).string();
    output_file.variables_.reserve(output_variables.size());
    output_file.stream_.open(output_path);
    if (!output_file.stream_.is_open()) {
        throw std::runtime_error("unable to open BMI output file '" + output_path.string() +
                                 "'");
    }

    output_file.stream_ << "time";
    for (const std::string& name : output_variables) {
        const std::size_t n_items =
            static_cast<std::size_t>(model.GetVarNbytes(name) / model.GetVarItemsize(name));

        if (n_items == 1) {
            output_file.stream_ << "," << name;
        } else {
            for (std::size_t i = 0; i < n_items; ++i) {
                output_file.stream_ << "," << name << "_" << i;
            }
        }
        output_file.variables_.push_back(OutputVariable{name, n_items});
    }
    output_file.stream_ << "\n";
    return output_file;
}

// Write one timestep row to a BMI output CSV
void write_output_step(::bmi::Bmi& model, OutputFile& output_file, double time) {
    output_file.stream_ << std::setprecision(std::numeric_limits<double>::max_digits10)
                        << time;
    for (const OutputVariable& variable : output_file.variables_) {
        const std::string type = model.GetVarType(variable.name_);
        if (type == "double") {
            write_var_values<double>(model, variable.name_, variable.n_items_,
                                     output_file.stream_);
        } else if (type == "float") {
            write_var_values<float>(model, variable.name_, variable.n_items_,
                                    output_file.stream_);
        } else if (type == "int") {
            write_var_values<int>(model, variable.name_, variable.n_items_,
                                  output_file.stream_);
        } else {
            throw std::runtime_error("unsupported BMI variable type '" + type +
                                     "' for variable '" + variable.name_ + "'");
        }
    }
    output_file.stream_ << "\n";
}

} // namespace miaow::bmi
