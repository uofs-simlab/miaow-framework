#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <caf/expected.hpp>
#include <caf/json_object.hpp>
#include <caf/json_value.hpp>

namespace {

struct RunnerOptions {
    std::string backend_;
    std::string plugin_path_;
    std::string config_file_;
    std::string output_dir_;
    std::string result_path_;
    std::string forcing_file_;
    std::map<std::string, std::string> variables_names_map_;
};

std::string require_value(int argc, char* argv[], int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + argv[index]);
    }
    ++index;
    return argv[index];
}

std::string parse_spec_path(int argc, char* argv[]) {
    std::string spec_path;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--spec") {
            spec_path = require_value(argc, argv, index);
        } else {
            throw std::runtime_error("unknown argument '" + arg + "'");
        }
    }

    if (spec_path.empty()) {
        throw std::runtime_error("missing --spec");
    }
    return spec_path;
}

caf::json_object require_object(const caf::json_value& value,
                                const std::string& context) {
    if (!value.is_object()) {
        throw std::runtime_error(context + " must be a JSON object");
    }
    return value.to_object();
}

std::string require_string(const caf::json_object& object,
                           const std::string& key,
                           bool allow_empty = false) {
    const caf::json_value value = object.value(key);
    if (value.is_undefined()) {
        throw std::runtime_error("spec is missing key '" + key + "'");
    }
    if (!value.is_string()) {
        throw std::runtime_error("spec key '" + key + "' must be a string");
    }

    const std::string parsed = std::string(value.to_string());
    if (!allow_empty && parsed.empty()) {
        throw std::runtime_error(
            "spec key '" + key + "' must be a non-empty string");
    }
    return parsed;
}

std::string optional_string(const caf::json_object& object,
                            const std::string& key) {
    const caf::json_value value = object.value(key);
    if (value.is_undefined()) {
        return "";
    }
    if (!value.is_string()) {
        throw std::runtime_error("spec key '" + key + "' must be a string");
    }

    const std::string parsed = std::string(value.to_string());
    if (parsed.empty()) {
        throw std::runtime_error(
            "spec key '" + key + "' must be a non-empty string");
    }
    return parsed;
}

std::map<std::string, std::string> parse_variables_names_map(
    const caf::json_object& root) {
    const caf::json_value value = root.value("variables_names_map");
    if (value.is_undefined()) {
        return {};
    }

    const caf::json_object map_object =
        require_object(value, "spec key 'variables_names_map'");
    std::map<std::string, std::string> variables_names_map;
    for (const auto [raw_input_name, header_value] : map_object) {
        const std::string input_name = std::string(raw_input_name);
        if (input_name.empty()) {
            throw std::runtime_error(
                "spec key 'variables_names_map' contains an empty key");
        }
        if (!header_value.is_string()) {
            throw std::runtime_error(
                "spec key 'variables_names_map' entry '" + input_name +
                "' must be a string");
        }
        const std::string header = std::string(header_value.to_string());
        if (header.empty()) {
            throw std::runtime_error(
                "spec key 'variables_names_map' entry '" + input_name +
                "' must be a non-empty string");
        }
        variables_names_map.emplace(input_name, header);
    }
    return variables_names_map;
}

RunnerOptions read_spec(const std::string& spec_path) {
    const std::filesystem::path normalized_spec_path =
        std::filesystem::absolute(spec_path).lexically_normal();
    const caf::expected<caf::json_value> parsed =
        caf::json_value::parse_file(normalized_spec_path.string());
    if (!parsed) {
        throw std::runtime_error("failed to parse spec file '" +
                                 normalized_spec_path.string() + "'");
    }

    const caf::json_object root = require_object(*parsed, "spec");
    RunnerOptions options;
    options.backend_ = require_string(root, "backend");
    options.plugin_path_ = require_string(root, "plugin_path");
    options.config_file_ = require_string(root, "config");
    options.output_dir_ = require_string(root, "output_dir", true);
    options.result_path_ =
        (normalized_spec_path.parent_path() / "bmi_subprocess_result.txt")
            .string();
    options.forcing_file_ = optional_string(root, "forcing_file");
    options.variables_names_map_ = parse_variables_names_map(root);
    if (options.forcing_file_.empty() &&
        !options.variables_names_map_.empty()) {
        throw std::runtime_error(
            "spec key 'variables_names_map' requires key 'forcing_file'");
    }
    return options;
}

miaow::Task make_task(const RunnerOptions& options) {
    miaow::Task task;
    task.args_.emplace_back(options.config_file_);
    task.task_output_dir_ = options.output_dir_;
    if (!options.forcing_file_.empty()) {
        task.args_.push_back(
            miaow::make_forcing_config(options.forcing_file_,
                                       options.variables_names_map_));
    }
    return task;
}

miaow::TaskResult run_bmi_task(const RunnerOptions& options) {
    miaow::Task task = make_task(options);
    if (options.backend_ == "cpp") {
        miaow::DynamicCppBmi model(options.plugin_path_);
        return miaow::execute_bmi_model(model, task);
    }
    if (options.backend_ == "fortran") {
        miaow::DynamicFortranBmi model(options.plugin_path_);
        return miaow::execute_bmi_model(model, task);
    }
    throw std::runtime_error("unknown backend '" + options.backend_ + "'");
}

void write_result(const std::string& result_path,
                  const miaow::TaskResult& task_result) {
    std::ofstream output(result_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open result file '" + result_path + "'");
    }

    output << miaow::task_status_to_string(task_result.status_) << "\n"
           << task_result.message_ << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    RunnerOptions options;
    try {
        options = read_spec(parse_spec_path(argc, argv));
    } catch (const std::exception& exception) {
        std::cerr << "BMI subprocess runner error: " << exception.what() << "\n";
        return 2;
    }

    miaow::TaskResult task_result;
    try {
        task_result = run_bmi_task(options);
    } catch (const std::exception& exception) {
        task_result.status_ = miaow::TaskStatus::failed;
        task_result.message_ =
            std::string("BMI error: ") + exception.what();
    } catch (...) {
        task_result.status_ = miaow::TaskStatus::failed;
        task_result.message_ = "BMI error: unknown exception";
    }

    try {
        write_result(options.result_path_, task_result);
    } catch (const std::exception& exception) {
        std::cerr << "BMI subprocess runner error: " << exception.what() << "\n";
        return 2;
    }

    return 0;
}
