#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "io/internal.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#define BOOST_PROCESS_USE_STD_FS
#include <boost/asio/io_context.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <caf/config_value.hpp>
#include <caf/json.hpp>

namespace miaow {

namespace {

#ifndef MIAOW_SUBPROCESS_PATH
#define MIAOW_SUBPROCESS_PATH ""
#endif

constexpr const char* bmi_file_prefix = "bmi_subprocess";
constexpr const char* bmi_temp_dir_prefix = "miaow_bmi_subprocess";

// Generate unique temporary control directories for tasks without output directories
std::atomic<unsigned long long> next_control_dir_id{0};

struct BmiSubprocessPaths {
    // Parent-written JSON payload consumed by the subprocess
    std::filesystem::path task_path_;

    // Child-written JSON task result consumed by the parent
    std::filesystem::path result_path_;

    // Child stdout log used for diagnostics
    std::filesystem::path stdout_path_;

    // Child stderr log used when subprocess setup or execution fails
    std::filesystem::path stderr_path_;

    // True when the control directory was created only for this task
    bool temporary_control_dir_{false};
};

// Reuse the job directory when the runtime has assigned result output
std::filesystem::path make_control_dir(const Task& task) {
    if (!task.task_output_dir_.empty()) {
        const std::filesystem::path results_directory =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        const std::filesystem::path job_directory = results_directory.parent_path();
        std::filesystem::create_directories(job_directory);
        return job_directory;
    }

    // Generate a unique temporary control directory when no output directory exists
    const unsigned long long id =
        next_control_dir_id.fetch_add(1, std::memory_order_relaxed);
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
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
    const std::string output_name =
        task.task_output_name_.empty() ? "task" : task.task_output_name_;
    const std::string control_stem = std::string(bmi_file_prefix) + "_" + output_name;

    BmiSubprocessPaths paths;
    paths.task_path_ = control_dir / (control_stem + "_input.json");
    paths.result_path_ = control_dir / (control_stem + "_result.json");
    paths.stdout_path_ = control_dir / (control_stem + "_stdout.log");
    paths.stderr_path_ = control_dir / (control_stem + "_stderr.log");
    paths.temporary_control_dir_ = task.task_output_dir_.empty();
    return paths;
}

// Include a bounded stderr prefix in parent-side failure messages
std::string read_file_prefix(const std::filesystem::path& path, std::size_t max_chars) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return "";
    }

    std::string text(max_chars, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

// Convert the subprocess JSON result into the parent task result contract
TaskResult read_subprocess_result(const std::filesystem::path& result_path) {
    caf::json_reader reader;
    if (!reader.load_file(result_path.string())) {
        throw std::runtime_error("failed to parse subprocess result file '" +
                                 result_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }

    TaskResult task_result;
    if (!reader.apply(task_result)) {
        throw std::runtime_error("failed to read subprocess result file '" +
                                 result_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }

    return task_result;
}

// Remove parent-child control files after a succeeded task result
void remove_subprocess_files(const BmiSubprocessPaths& paths) {
    std::error_code ignored_error;
    std::filesystem::remove(paths.task_path_, ignored_error);
    std::filesystem::remove(paths.result_path_, ignored_error);
    std::filesystem::remove(paths.stdout_path_, ignored_error);
    std::filesystem::remove(paths.stderr_path_, ignored_error);
    if (paths.temporary_control_dir_) {
        std::filesystem::remove(paths.task_path_.parent_path(), ignored_error);
    }
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
TaskResult execute_subprocess_task(const BmiSubprocessPaths& paths,
                                   const std::string& subprocess_path) {
    // Specify command-line args to be passed to the subprocess executable
    const std::vector<std::string> args{
        "--task",
        paths.task_path_.string(),
    };

    // Create the child process and redirect stdout and stderr to specified paths
    boost::asio::io_context io_context;
    boost::process::v2::process child_process(
        io_context, subprocess_path, args,
        boost::process::v2::process_stdio{{}, paths.stdout_path_, paths.stderr_path_});

    // Execute the child process and wait for it to complete
    const int status = child_process.wait();

    // If the subprocess wrote a result file, parse it into the task result contract
    if (std::filesystem::exists(paths.result_path_)) {
        TaskResult task_result = read_subprocess_result(paths.result_path_);
        if (task_result.status_ == TaskStatus::succeeded) {
            remove_subprocess_files(paths);
        }
        return task_result;
    }

    // If the subprocess did not write a result file, capture stderr and rethrow
    std::string message = "subprocess exited with status " + std::to_string(status);
    const std::string stderr_text = read_file_prefix(paths.stderr_path_, 2000);
    if (!stderr_text.empty()) {
        message += ": " + stderr_text;
    }
    throw std::runtime_error(message);
}

// Write the parent-to-subprocess task payload
void write_subprocess_task(const std::filesystem::path& task_path, const Task& task) {
    Task task_payload = task;
    if (!task_payload.task_output_dir_.empty()) {
        task_payload.task_output_dir_ =
            std::filesystem::absolute(task_payload.task_output_dir_)
                .lexically_normal()
                .string();
    }

    io::write_json_file(task_path, task_payload, false);
}

} // namespace

namespace bmi {

// Execute one BMI pipeline lifecycle in a subprocess
TaskResult execute_bmi_lifecycle_in_subprocess(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "subprocess BMI task must contain exactly one top-level BMI "
            "pipeline config argument");
    }
    if (caf::get_if<caf::config_value::dictionary>(&task.args_.front()) == nullptr) {
        throw std::runtime_error(
            "subprocess BMI task argument must be a BmiPipelineConfig");
    }

    const std::string subprocess_path = configured_subprocess_path();
    if (subprocess_path.empty()) {
        throw std::runtime_error("MIAOW_SUBPROCESS_PATH is not configured");
    }

    const BmiSubprocessPaths paths = make_subprocess_paths(task);
    write_subprocess_task(paths.task_path_, task);

    return execute_subprocess_task(paths, subprocess_path);
}

} // namespace bmi

} // namespace miaow
