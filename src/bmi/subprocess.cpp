#include "miaow_bmi.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace miaow {

namespace {

#ifndef MIAOW_BMI_RUNNER_PATH
#define MIAOW_BMI_RUNNER_PATH ""
#endif

std::atomic<unsigned long long> next_control_dir_id{0};

TaskResult make_failed_result(const Task& task, const std::string& message) {
    TaskResult task_result;
    task_result.simulation_unit_id_ = task.simulation_unit_id_;
    task_result.status_ = TaskStatus::failed;
    task_result.message_ = message;
    return task_result;
}

std::string backend_name(DynamicBmiBackend backend) {
    if (backend == DynamicBmiBackend::cpp) {
        return "cpp";
    }
    return "fortran";
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::filesystem::path make_control_dir(const Task& task) {
    if (!task.task_output_dir_.empty()) {
        std::filesystem::path task_output_dir =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        std::filesystem::create_directories(task_output_dir);
        return task_output_dir;
    }

    const unsigned long long id =
        next_control_dir_id.fetch_add(1, std::memory_order_relaxed);
    const long long stamp = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
    const std::filesystem::path control_dir =
        std::filesystem::temp_directory_path() /
        ("miaow_bmi_subprocess_" + std::to_string(stamp) + "_" +
         std::to_string(id));
    std::filesystem::create_directories(control_dir);
    return control_dir;
}

std::string read_file_prefix(const std::filesystem::path& path,
                             std::size_t max_chars) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return "";
    }

    std::string text(max_chars, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

std::string runner_path() {
    const char* env_path = std::getenv("MIAOW_BMI_RUNNER_PATH");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    return MIAOW_BMI_RUNNER_PATH;
}

std::string require_string(const caf::config_value::dictionary& dict,
                           const std::string& key) {
    const auto it = dict.find(key);
    if (it == dict.end()) {
        throw std::runtime_error("missing forcing key '" + key + "'");
    }

    const std::string* value = caf::get_if<std::string>(&it->second);
    if (value == nullptr || value->empty()) {
        throw std::runtime_error("forcing key '" + key +
                                 "' must be a non-empty string");
    }
    return *value;
}

void write_json_string(std::ostream& output, const std::string& value) {
    output << '"';
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            output << '\\' << ch;
        } else if (ch == '\n') {
            output << "\\n";
        } else if (ch == '\r') {
            output << "\\r";
        } else if (ch == '\t') {
            output << "\\t";
        } else {
            output << ch;
        }
    }
    output << '"';
}

void write_string_field(std::ostream& output,
                        const std::string& key,
                        const std::string& value,
                        bool& needs_comma) {
    if (needs_comma) {
        output << ",\n";
    }
    output << "  ";
    write_json_string(output, key);
    output << ": ";
    write_json_string(output, value);
    needs_comma = true;
}

void write_variables_names_map(std::ostream& output,
                               const caf::config_value::dictionary& map,
                               bool& needs_comma) {
    if (needs_comma) {
        output << ",\n";
    }
    output << "  \"variables_names_map\": {\n";

    bool needs_map_comma = false;
    for (const auto& [input_name, header_value] : map) {
        const std::string* header = caf::get_if<std::string>(&header_value);
        if (header == nullptr) {
            throw std::runtime_error(
                "forcing map entry '" + input_name + "' must be a string");
        }
        if (needs_map_comma) {
            output << ",\n";
        }
        output << "    ";
        write_json_string(output, input_name);
        output << ": ";
        write_json_string(output, *header);
        needs_map_comma = true;
    }

    output << "\n  }";
    needs_comma = true;
}

void write_subprocess_spec(const std::filesystem::path& spec_path,
                           DynamicBmiBackend backend,
                           const std::string& plugin_path,
                           const std::string& config_file,
                           const Task& task) {
    std::ofstream output(spec_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open subprocess spec '" +
                                 spec_path.string() + "'");
    }

    bool needs_comma = false;
    const std::string output_dir = task.task_output_dir_.empty()
        ? ""
        : std::filesystem::absolute(task.task_output_dir_)
              .lexically_normal()
              .string();
    output << "{\n";
    write_string_field(output, "backend", backend_name(backend), needs_comma);
    write_string_field(output, "plugin_path", plugin_path, needs_comma);
    write_string_field(output, "config", config_file, needs_comma);
    write_string_field(output, "output_dir", output_dir, needs_comma);

    if (task.args_.size() >= 2) {
        const caf::config_value::dictionary* forcing_config =
            caf::get_if<caf::config_value::dictionary>(&task.args_.at(1));
        if (forcing_config == nullptr) {
            throw std::runtime_error(
                "second task argument must be a forcing dictionary");
        }

        write_string_field(output,
                           "forcing_file",
                           require_string(*forcing_config, "forcing_file"),
                           needs_comma);

        const auto map_it = forcing_config->find("variables_names_map");
        if (map_it != forcing_config->end()) {
            const caf::config_value::dictionary* variables_names_map =
                caf::get_if<caf::config_value::dictionary>(&map_it->second);
            if (variables_names_map == nullptr) {
                throw std::runtime_error(
                    "forcing key 'variables_names_map' must be a dictionary");
            }
            write_variables_names_map(output, *variables_names_map, needs_comma);
        }
    }

    output << "\n}\n";
}

std::string make_command(const std::vector<std::string>& args,
                         const std::filesystem::path& stdout_path,
                         const std::filesystem::path& stderr_path) {
    std::string command;
    for (const std::string& arg : args) {
        if (!command.empty()) {
            command += " ";
        }
        command += shell_quote(arg);
    }
    command += " > " + shell_quote(stdout_path.string());
    command += " 2> " + shell_quote(stderr_path.string());
    return command;
}

TaskResult read_runner_result(const Task& task,
                              const std::filesystem::path& result_path) {
    std::ifstream input(result_path);
    if (!input.is_open()) {
        throw std::runtime_error("runner did not write a result file");
    }

    std::string status;
    std::getline(input, status);

    std::ostringstream message_stream;
    message_stream << input.rdbuf();
    std::string message = message_stream.str();
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }

    TaskResult task_result;
    task_result.simulation_unit_id_ = task.simulation_unit_id_;
    task_result.status_ = status == "succeeded"
        ? TaskStatus::succeeded
        : TaskStatus::failed;
    task_result.message_ = std::move(message);
    return task_result;
}

} // namespace

TaskResult execute_dynamic_bmi_model_in_subprocess(
    DynamicBmiBackend backend,
    const std::string& plugin_path,
    const Task& task) {
    try {
        const std::string selected_runner_path = runner_path();
        if (selected_runner_path.empty()) {
            return make_failed_result(
                task,
                "BMI subprocess error: MIAOW_BMI_RUNNER_PATH is not configured");
        }
        if (plugin_path.empty()) {
            return make_failed_result(
                task,
                "BMI subprocess error: dynamic BMI plugin path is empty");
        }
        if (task.args_.empty()) {
            return make_failed_result(
                task,
                "BMI subprocess error: missing config path in task specification");
        }

        const std::string* config_file =
            caf::get_if<std::string>(&task.args_.front());
        if (config_file == nullptr) {
            return make_failed_result(
                task,
                "BMI subprocess error: first task argument must be a config path string");
        }

        const std::filesystem::path control_dir = make_control_dir(task);
        const std::filesystem::path result_path =
            control_dir / "bmi_subprocess_result.txt";
        const std::filesystem::path spec_path =
            control_dir / "bmi_subprocess_spec.json";
        const std::filesystem::path stdout_path =
            control_dir / "bmi_subprocess_stdout.txt";
        const std::filesystem::path stderr_path =
            control_dir / "bmi_subprocess_stderr.txt";

        write_subprocess_spec(spec_path,
                              backend,
                              plugin_path,
                              *config_file,
                              task);

        std::vector<std::string> args{
            selected_runner_path,
            "--spec",
            spec_path.string(),
        };

        const int status = std::system(
            make_command(args, stdout_path, stderr_path).c_str());
        if (std::filesystem::exists(result_path)) {
            return read_runner_result(task, result_path);
        }

        std::string message =
            "BMI subprocess error: runner exited with status " +
            std::to_string(status);
        const std::string stderr_text = read_file_prefix(stderr_path, 2000);
        if (!stderr_text.empty()) {
            message += ": " + stderr_text;
        }
        return make_failed_result(task, message);
    } catch (const std::exception& exception) {
        return make_failed_result(
            task,
            std::string("BMI subprocess error: ") + exception.what());
    }
}

} // namespace miaow
