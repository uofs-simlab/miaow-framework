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
#include <stdexcept>
#include <string>
#include <utility>
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

constexpr const char* bmi_temp_dir_prefix = "miaow_bmi_subprocess";

// Generate unique temporary control directories for tasks without output directories
std::atomic<unsigned long long> next_control_dir_id{0};

struct BmiSubprocessPaths {
    std::filesystem::path spec_path_;
    std::filesystem::path result_path_;
    std::filesystem::path stdout_path_;
    std::filesystem::path stderr_path_;
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

const bmi::BmiPipelineConfig& require_bmi_pipeline_config_argument(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "subprocess BMI task must contain exactly one top-level BMI "
            "pipeline config argument");
    }
    const bmi::BmiPipelineConfig* pipeline_config =
        caf::get_if<caf::config_value::dictionary>(&task.args_.front());
    if (pipeline_config == nullptr) {
        throw std::runtime_error(
            "subprocess BMI task argument must be a top-level BMI pipeline "
            "config dictionary");
    }
    return *pipeline_config;
}

// Create the task-local files used by one BMI subprocess execution
BmiSubprocessPaths make_subprocess_paths(const Task& task) {
    const std::filesystem::path control_dir = make_control_dir(task);
    const std::string output_name =
        task.task_output_name_.empty() ? "task" : task.task_output_name_;
    const std::string control_stem = std::string(bmi::bmi_file_prefix) + "_" + output_name;

    BmiSubprocessPaths paths;
    paths.spec_path_ = control_dir / (control_stem + "_spec.json");
    paths.result_path_ = bmi::make_subprocess_result_path(paths.spec_path_);
    paths.stdout_path_ = control_dir / (control_stem + "_stdout.log");
    paths.stderr_path_ = control_dir / (control_stem + "_stderr.log");
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
        "--spec",
        paths.spec_path_.string(),
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
        return read_subprocess_result(paths.result_path_);
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
                           DynamicBmiBackend backend, const std::string& plugin_path,
                           const std::string& cpp_create_function,
                           const std::string& cpp_destroy_function,
                           const bmi::BmiPipelineConfig& pipeline_config,
                           const Task& task) {
    bmi::BmiSubprocessTaskSpec subprocess_task;
    subprocess_task.pipeline_config_ = pipeline_config;
    subprocess_task.module_.dynamic_backend_ = backend;
    subprocess_task.module_.plugin_path_ = plugin_path;
    subprocess_task.output_dir_ =
        task.task_output_dir_.empty()
            ? ""
            : std::filesystem::absolute(task.task_output_dir_).lexically_normal().string();
    subprocess_task.output_name_ = task.task_output_name_;

    if (backend == DynamicBmiBackend::cpp) {
        subprocess_task.module_.cpp_create_function_ = cpp_create_function;
        subprocess_task.module_.cpp_destroy_function_ = cpp_destroy_function;
    }

    io::write_json_file(spec_path, subprocess_task);
}

} // namespace

// Execute a dynamic BMI model in a subprocess and return the result
TaskResult execute_dynamic_bmi_model_in_subprocess(DynamicBmiBackend backend,
                                                   const std::string& plugin_path,
                                                   const Task& task,
                                                   const std::string& create_function,
                                                   const std::string& destroy_function) {
    if (plugin_path.empty()) {
        throw std::runtime_error("dynamic BMI plugin path is empty");
    }
    const bmi::BmiPipelineConfig& pipeline_config =
        require_bmi_pipeline_config_argument(task);

    const std::string subprocess_path = configured_subprocess_path();
    if (subprocess_path.empty()) {
        throw std::runtime_error("MIAOW_SUBPROCESS_PATH is not configured");
    }

    const BmiSubprocessPaths paths = make_subprocess_paths(task);
    write_subprocess_task(paths.spec_path_, backend, plugin_path, create_function,
                          destroy_function, pipeline_config, task);

    return execute_subprocess_task(paths, subprocess_path);
}

} // namespace miaow
