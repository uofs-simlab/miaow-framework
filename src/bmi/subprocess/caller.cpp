#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define BOOST_PROCESS_USE_STD_FS
#include <boost/asio/io_context.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <caf/config_value.hpp>

namespace miaow {

namespace {

#ifndef MIAOW_SUBPROCESS_PATH
#define MIAOW_SUBPROCESS_PATH ""
#endif

constexpr const char* bmi_temp_dir_prefix = "miaow_bmi_subprocess";

// Generate unique temporary control directories for tasks without output directories
std::atomic<unsigned long long> next_control_dir_id{0};

struct BmiSubprocessPaths {
    std::filesystem::path spec_path_;
    std::filesystem::path result_path_;
    std::filesystem::path stdout_path_;
    std::filesystem::path stderr_path_;
};

// Reuse the task output directory when the runtime has assigned one
std::filesystem::path make_control_dir(const Task& task) {
    if (!task.task_output_dir_.empty()) {
        const std::filesystem::path task_output_dir =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        std::filesystem::create_directories(task_output_dir);
        return task_output_dir;
    }

    // Generate a unique temporary control directory when no output directory exists
    const unsigned long long id =
        next_control_dir_id.fetch_add(1, std::memory_order_relaxed);
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    const long long stamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const std::filesystem::path control_dir =
        std::filesystem::temp_directory_path() /
        (std::string(bmi_temp_dir_prefix) + "_" + std::to_string(stamp) + "_" +
         std::to_string(id));
    std::filesystem::create_directories(control_dir);
    return control_dir;
}

// Create the task-local files used by one BMI subprocess execution
BmiSubprocessPaths make_subprocess_paths(const Task& task) {
    const std::filesystem::path control_dir = make_control_dir(task);

    BmiSubprocessPaths paths;
    paths.spec_path_ =
        control_dir / (std::string(bmi::bmi_file_prefix) + "_spec.json");
    paths.result_path_ = bmi::make_subprocess_result_path(paths.spec_path_);
    paths.stdout_path_ =
        control_dir / (std::string(bmi::bmi_file_prefix) + "_stdout.txt");
    paths.stderr_path_ =
        control_dir / (std::string(bmi::bmi_file_prefix) + "_stderr.txt");
    return paths;
}

// Include a bounded stderr prefix in parent-side failure messages
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

// Convert the subprocess text result into the parent task result contract
TaskResult read_subprocess_result(const Task& task,
                                  const std::filesystem::path& result_path) {
    std::ifstream input(result_path);
    if (!input.is_open()) {
        throw std::runtime_error("subprocess did not write a result file");
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

// Return the configured subprocess executable path
std::string configured_subprocess_path() {
    const char* env_path = std::getenv("MIAOW_SUBPROCESS_PATH");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    return MIAOW_SUBPROCESS_PATH;
}

// Launch the subprocess executable using Boost.Process and wait for the result file
TaskResult execute_subprocess_task(const Task& task,
                                   const BmiSubprocessPaths& paths,
                                   const std::string& subprocess_path) {
    // Specify command-line args to be passed to the subprocess executable
    const std::vector<std::string> args{
        "--spec",
        paths.spec_path_.string(),
    };

    // Create the child process and redirect stdout and stderr to specified paths
    boost::asio::io_context io_context;
    boost::process::v2::process child_process(
        io_context,
        subprocess_path,
        args,
        boost::process::v2::process_stdio{{},
                                          paths.stdout_path_,
                                          paths.stderr_path_});

    // Execute the child process and wait for it to complete
    const int status = child_process.wait();

    // If the subprocess wrote a result file, parse it into the task result contract
    if (std::filesystem::exists(paths.result_path_)) {
        return read_subprocess_result(task, paths.result_path_);
    }

    // If the subprocess did not write a result file, capture stderr and rethrow
    std::string message = "subprocess exited with status " + std::to_string(status);
    const std::string stderr_text = read_file_prefix(paths.stderr_path_, 2000);
    if (!stderr_text.empty()) {
        message += ": " + stderr_text;
    }
    throw std::runtime_error(message);
}

// Write the parent-to-subprocess BMI task payload
void write_subprocess_task(const std::filesystem::path& spec_path,
                           DynamicBmiBackend backend,
                           const std::string& plugin_path,
                           const std::string& cpp_create_function,
                           const std::string& cpp_destroy_function,
                           const std::string& config_file,
                           const Task& task) {
    bmi::BmiSubprocessTaskSpec subprocess_task;
    subprocess_task.backend_ =
        backend == DynamicBmiBackend::cpp ? "cpp" : "fortran";
    subprocess_task.plugin_path_ = plugin_path;
    subprocess_task.config_file_ = config_file;
    subprocess_task.output_dir_ = task.task_output_dir_.empty()
        ? ""
        : std::filesystem::absolute(task.task_output_dir_)
              .lexically_normal()
              .string();

    if (backend == DynamicBmiBackend::cpp) {
        subprocess_task.cpp_create_function_ = cpp_create_function;
        subprocess_task.cpp_destroy_function_ = cpp_destroy_function;
    }

    if (task.args_.size() >= 2) {
        const caf::config_value::dictionary* forcing_config =
            caf::get_if<caf::config_value::dictionary>(&task.args_.at(1));
        if (forcing_config == nullptr) {
            throw std::runtime_error(
                "second task argument must be a forcing dictionary");
        }

        const caf::config_value::dictionary::const_iterator forcing_file_it =
            forcing_config->find("forcing_file");
        if (forcing_file_it == forcing_config->end()) {
            throw std::runtime_error("missing forcing key 'forcing_file'");
        }
        const std::string* forcing_file =
            caf::get_if<std::string>(&forcing_file_it->second);
        if (forcing_file == nullptr || forcing_file->empty()) {
            throw std::runtime_error(
                "forcing key 'forcing_file' must be a non-empty string");
        }
        subprocess_task.forcing_file_ = *forcing_file;

        const caf::config_value::dictionary::const_iterator map_it =
            forcing_config->find("variables_names_map");
        if (map_it != forcing_config->end()) {
            const caf::config_value::dictionary* variables_names_map =
                caf::get_if<caf::config_value::dictionary>(&map_it->second);
            if (variables_names_map == nullptr) {
                throw std::runtime_error(
                    "forcing key 'variables_names_map' must be a dictionary");
            }
            subprocess_task.variables_names_map_.emplace();
            for (const std::pair<const std::string, caf::config_value>&
                     map_entry : *variables_names_map) {
                const std::string* header =
                    caf::get_if<std::string>(&map_entry.second);
                if (header == nullptr) {
                    throw std::runtime_error(
                        "forcing map entry '" + map_entry.first +
                        "' must be a string");
                }
                subprocess_task.variables_names_map_->emplace(
                    map_entry.first,
                    *header);
            }
        }
    }

    io::write_json_file(spec_path, subprocess_task);
}

} // namespace

// Execute a dynamic BMI model in a subprocess and return the result
TaskResult execute_dynamic_bmi_model_in_subprocess(
    DynamicBmiBackend backend,
    const std::string& plugin_path,
    const Task& task,
    const std::string& create_function,
    const std::string& destroy_function) {
    if (plugin_path.empty()) {
        throw std::runtime_error("dynamic BMI plugin path is empty");
    }
    if (task.args_.empty()) {
        throw std::runtime_error("missing config path in task specification");
    }

    const std::string* config_file =
        caf::get_if<std::string>(&task.args_.front());
    if (config_file == nullptr) {
        throw std::runtime_error("first task argument must be a config path string");
    }

    const std::string subprocess_path = configured_subprocess_path();
    if (subprocess_path.empty()) {
        throw std::runtime_error("MIAOW_SUBPROCESS_PATH is not configured");
    }

    const BmiSubprocessPaths paths = make_subprocess_paths(task);
    write_subprocess_task(paths.spec_path_,
                          backend,
                          plugin_path,
                          create_function,
                          destroy_function,
                          *config_file,
                          task);

    return execute_subprocess_task(task, paths, subprocess_path);
}

} // namespace miaow
