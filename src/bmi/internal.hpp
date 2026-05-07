#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <caf/config_value.hpp>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

inline constexpr const char* bmi_file_prefix = "bmi_subprocess";

// JSON task payload passed from the parent process to the subprocess executable
struct BmiSubprocessTaskSpec {
    std::string backend_;
    std::string plugin_path_;
    std::string config_file_;
    std::string output_dir_;
    std::optional<std::string> cpp_create_function_;
    std::optional<std::string> cpp_destroy_function_;
    std::optional<std::string> forcing_file_;
    std::optional<std::map<std::string, std::string>> variables_names_map_;
};

// Serialize the subprocess task payload
template <class Inspector>
bool inspect(Inspector& inspector, BmiSubprocessTaskSpec& subprocess_task) {
    return inspector.object(subprocess_task).fields(
        inspector.field("backend", subprocess_task.backend_),
        inspector.field("plugin_path", subprocess_task.plugin_path_),
        inspector.field("config", subprocess_task.config_file_),
        inspector.field("output_dir", subprocess_task.output_dir_),
        inspector.field("cpp_create_function",
                        subprocess_task.cpp_create_function_),
        inspector.field("cpp_destroy_function",
                        subprocess_task.cpp_destroy_function_),
        inspector.field("forcing_file", subprocess_task.forcing_file_),
        inspector.field("variables_names_map",
                        subprocess_task.variables_names_map_));
}

// Return the result path that corresponds to a subprocess spec path
inline std::filesystem::path make_subprocess_result_path(
    const std::filesystem::path& spec_path) {
    return spec_path.parent_path() / (std::string(bmi_file_prefix) + "_result.txt");
}

// Open stream-backed CSV outputs for each BMI output variable
struct OutputVariable {
    std::string name_;              // BMI output variable name
    std::size_t n_items_;           // Number of scalar items in the variable value
    std::ofstream stream_;          // CSV output stream for this variable
};

// Construct an initialized BMI-compliant forcing source from the structured
// forcing dictionary in `Task.args_[1]`
std::unique_ptr<::bmi::Bmi> make_forcing_bmi(
    const caf::config_value::dictionary& forcing_config,
    ::bmi::Bmi& model);

// Open CSV output files for all BMI output variables in output_dir
std::vector<OutputVariable> open_output_vars(
    ::bmi::Bmi& model,
    const std::string& output_dir);

// Write one timestep row to a variable CSV stream
void write_output_step(::bmi::Bmi& model, OutputVariable& variable, double time);

} // namespace miaow::bmi
