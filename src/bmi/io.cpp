#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace miaow {

miaow::BmiPipelineConfig
make_bmi_pipeline_config(std::vector<BmiModuleConfig> module_configs,
                         const std::string& forcing_file,
                         caf::config_value::dictionary feature) {
    if (module_configs.empty()) {
        throw std::runtime_error(
            "BMI pipeline config error: module_configs must contain at least one module");
    }

    miaow::BmiPipelineConfig pipeline_config;
    if (!feature.empty()) {
        pipeline_config.insert("feature", std::move(feature));
    }
    pipeline_config.insert("global",
                           bmi::make_bmi_multi_global_config(module_configs, forcing_file));
    return pipeline_config;
}

miaow::BmiPipelineConfig
make_bmi_model_config(const std::string& init_config, const std::string& forcing_file,
                      const std::map<std::string, std::string>& variables_names_map) {
    if (io::trim(init_config).empty()) {
        throw std::runtime_error(
            "BMI pipeline config error: init_config must be a non-empty string");
    }

    BmiModuleConfig module_config;
    module_config.name_ = "supplied_model";
    module_config.config_file_ = init_config;
    module_config.variables_names_map_ = variables_names_map;

    return make_bmi_pipeline_config({std::move(module_config)}, forcing_file);
}

} // namespace miaow

namespace miaow::bmi {

namespace {

constexpr const char* bmi_pipeline_config_temp_dir_prefix = "miaow_bmi_pipeline_config";

std::atomic<unsigned long long> next_bmi_pipeline_config_file_id{0};

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

// Build the BMI pipeline config filename written beside task results
std::string bmi_pipeline_config_filename(const Task& task) {
    const std::string output_name =
        task.task_output_name_.empty() ? "task" : task.task_output_name_;
    return output_name + ".json";
}

// Generate a temporary pipeline config path when the task has no output directory
std::filesystem::path make_temporary_bmi_pipeline_config_path() {
    const unsigned long long id =
        next_bmi_pipeline_config_file_id.fetch_add(1, std::memory_order_relaxed);
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    const long long stamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string(bmi_pipeline_config_temp_dir_prefix) + "_" + std::to_string(stamp) +
         "_" + std::to_string(id));
    std::filesystem::create_directories(directory);
    return directory / "pipeline_config.json";
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

BmiModuleConfig parse_bmi_module_config(const caf::json_object& module_object,
                                        std::string_view error_prefix) {
    BmiModuleConfig module_config;
    module_config.name_ = io::trim(std::string(module_object.value("name").to_string()));

    const caf::json_object module_params = module_object.value("params").to_object();
    module_config.config_file_ =
        io::trim(std::string(module_params.value("init_config").to_string()));
    module_config.variables_names_map_ =
        io::parse_json_string_map(module_params, "variables_names_map", error_prefix);

    // Supplied model modules are bound to a model from BmiSimulationUnit
    if (module_config.name_ == "supplied_model") {
        return module_config;
    }

    // Otherwise, it's a dynamic module that requires a plugin library path
    module_config.plugin_path_ =
        io::trim(std::string(module_params.value("library_file").to_string()));

    // Fortran backend
    if (module_config.name_ == "bmi_fortran") {
        module_config.dynamic_backend_ = DynamicBmiBackend::fortran;
        return module_config;
    }

    // C++ backend
    if (module_config.name_ == "bmi_c++" || module_config.name_ == "bmi_cpp" ||
        module_config.name_.rfind("bmi_c++", 0) == 0 ||
        module_config.name_.rfind("bmi_cpp", 0) == 0) {
        module_config.dynamic_backend_ = DynamicBmiBackend::cpp;
        module_config.cpp_create_function_ =
            io::optional_json_string(module_params, "cpp_create_function",
                                     "bmi_model_create", error_prefix);
        module_config.cpp_destroy_function_ =
            io::optional_json_string(module_params, "cpp_destroy_function",
                                     "bmi_model_destroy", error_prefix);
        return module_config;
    }

    io::throw_config_error(error_prefix,
                           "unsupported module '" + module_config.name_ + "'");
}

// Build the shared bmi_multi global block used by BMI pipeline configs
caf::config_value::dictionary
make_bmi_multi_global_config(const std::vector<BmiModuleConfig>& module_configs,
                             const std::string& forcing_file) {
    caf::config_value::list modules;
    for (const BmiModuleConfig& module_config : module_configs) {
        caf::config_value::dictionary module_params;
        if (module_config.dynamic_backend_) {
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

        caf::config_value::dictionary module_value;
        module_value.insert("name", module_config.name_);
        module_value.insert("params", std::move(module_params));
        modules.emplace_back(std::move(module_value));
    }

    caf::config_value::dictionary formulation_params;
    formulation_params.insert("output_module", caf::config_value::integer{0});
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

// Read the BMI pipeline config JSON consumed by the BMI pipeline
caf::json_object read_bmi_pipeline_config_file(const std::filesystem::path& path) {
    return io::read_json_object_file(path, "BMI pipeline config root",
                                     "unable to parse BMI pipeline config file",
                                     "BMI pipeline config error: ");
}

std::filesystem::path bmi_pipeline_config_file_path(const Task& task) {
    if (!task.task_output_dir_.empty()) {
        const std::filesystem::path results_directory =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        const std::filesystem::path job_directory = results_directory.parent_path();
        std::filesystem::create_directories(job_directory);
        return job_directory / bmi_pipeline_config_filename(task);
    }
    return make_temporary_bmi_pipeline_config_path();
}

std::vector<std::string> bmi_pipeline_config_artifact_paths(const Task& task) {
    if (task.task_output_dir_.empty()) {
        return {};
    }
    return {bmi_pipeline_config_filename(task)};
}

void write_bmi_pipeline_config_file(const miaow::BmiPipelineConfig& pipeline_config,
                                    const std::filesystem::path& path) {
    io::write_config_json_file(path, pipeline_config);
}

// Open one CSV file that contains all BMI output variables
OutputFile open_output_file(::bmi::Bmi& model, const std::string& output_dir,
                            const std::string& output_name) {
    if (output_name.empty()) {
        throw std::runtime_error("BMI output error: task output name is empty");
    }

    const std::filesystem::path output_directory(output_dir);
    const std::filesystem::path output_filename = output_name + ".csv";
    const std::filesystem::path output_path = output_directory / output_filename;

    const std::vector<std::string> var_names = model.GetOutputVarNames();
    OutputFile output_file;
    output_file.artifact_path_ = (output_directory.filename() / output_filename).string();
    output_file.variables_.reserve(var_names.size());
    output_file.stream_.open(output_path);
    if (!output_file.stream_.is_open()) {
        throw std::runtime_error("unable to open BMI output file '" + output_path.string() +
                                 "'");
    }

    output_file.stream_ << "time";
    for (const std::string& name : var_names) {
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
