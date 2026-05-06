#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define BOOST_PROCESS_USE_STD_FS
#include <boost/asio/io_context.hpp>
#include <boost/process/execute.hpp>
#include <boost/process/process.hpp>
#include <boost/process/stdio.hpp>
#include <caf/json.hpp>

namespace miaow::bmi {

// JSON contract passed from the parent process to the subprocess executable
struct BmiSubprocessSpec {
    std::string backend_;
    std::string plugin_path_;
    std::string config_file_;
    std::string output_dir_;
    std::optional<std::string> forcing_file_;
    std::optional<std::map<std::string, std::string>> variables_names_map_;
};

// Serialize the subprocess execution contract
template <class Inspector>
bool inspect(Inspector& inspector, BmiSubprocessSpec& spec) {
    return inspector.object(spec).fields(
        inspector.field("backend", spec.backend_),
        inspector.field("plugin_path", spec.plugin_path_),
        inspector.field("config", spec.config_file_),
        inspector.field("output_dir", spec.output_dir_),
        inspector.field("forcing_file", spec.forcing_file_),
        inspector.field("variables_names_map", spec.variables_names_map_));
}

} // namespace miaow::bmi

namespace miaow {

namespace {

#ifndef MIAOW_SUBPROCESS_PATH
#define MIAOW_SUBPROCESS_PATH ""
#endif

constexpr const char* bmi_file_prefix = "bmi_subprocess";
constexpr const char* bmi_subprocess_error_prefix = "BMI subprocess error";
constexpr const char* bmi_temp_dir_prefix = "miaow_bmi_subprocess";

// Generate unique temporary control directories for tasks without output directories
std::atomic<unsigned long long> next_control_dir_id{0};

struct BmiSubprocessPaths {
    std::filesystem::path spec_path_;
    std::filesystem::path result_path_;
    std::filesystem::path stdout_path_;
    std::filesystem::path stderr_path_;
};

// Parse a required CLI value immediately after its option name
std::string require_cli_value(int argc, char* argv[], int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + argv[index]);
    }
    ++index;
    return argv[index];
}

// Parse the subprocess executable command-line contract
std::filesystem::path parse_subprocess_spec_path(int argc, char* argv[]) {
    std::string spec_path;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--spec") {
            spec_path = require_cli_value(argc, argv, index);
        } else {
            throw std::runtime_error("unknown argument '" + arg + "'");
        }
    }

    if (spec_path.empty()) {
        throw std::runtime_error("missing --spec");
    }
    return std::filesystem::absolute(spec_path).lexically_normal();
}

// Reuse the task output directory when the runtime has assigned one
std::filesystem::path make_control_dir(const Task& task) {
    // Try to use the task output directory
    if (!task.task_output_dir_.empty()) {
        const std::filesystem::path task_output_dir =
            std::filesystem::absolute(task.task_output_dir_).lexically_normal();
        std::filesystem::create_directories(task_output_dir);
        return task_output_dir;
    }

    // If there is no task output directory, generate a unique temporary control directory
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

// Return the result path that corresponds to a subprocess spec path
std::filesystem::path make_subprocess_result_path(
    const std::filesystem::path& spec_path) {
    return spec_path.parent_path() / (std::string(bmi_file_prefix) + "_result.txt");
}

// Create the task-local files used by one BMI subprocess execution
BmiSubprocessPaths make_subprocess_paths(const Task& task) {
    const std::filesystem::path control_dir = make_control_dir(task);

    BmiSubprocessPaths paths;
    paths.spec_path_ = control_dir / (std::string(bmi_file_prefix) + "_spec.json");
    paths.result_path_ = make_subprocess_result_path(paths.spec_path_);
    paths.stdout_path_ = control_dir / (std::string(bmi_file_prefix) + "_stdout.txt");
    paths.stderr_path_ = control_dir / (std::string(bmi_file_prefix) + "_stderr.txt");
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

// Write the minimal result contract consumed by the parent process
void write_subprocess_result(const std::filesystem::path& result_path,
                             const TaskResult& task_result) {
    std::ofstream output(result_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open result file '" +
                                 result_path.string() + "'");
    }

    output << task_status_to_string(task_result.status_) << "\n"
           << task_result.message_ << "\n";
}

// Write child-process setup failures through the diagnostic stderr file
void write_subprocess_error(const std::string& message) {
    std::cerr << bmi_subprocess_error_prefix
              << ": subprocess setup error: " << message << "\n";
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
TaskResult run_subprocess_task(const Task& task,
                               const BmiSubprocessPaths& paths,
                               const std::string& subprocess_path) {
    // Specify command-line args to be passed to the subprocess executable
    const std::vector<std::string> args{
        "--spec",
        paths.spec_path_.string(),
    };

    // Create the child process and redirect stdout and stderr to specified paths
    boost::asio::io_context io_context;
    boost::process::process child_process(
        io_context,
        subprocess_path,
        args,
        boost::process::process_stdio{{},
                                      paths.stdout_path_,
                                      paths.stderr_path_});

    // Execute the child process and wait for it to complete
    const int status = boost::process::execute(std::move(child_process));

    // If the subprocess wrote a result file, parse it into the task result contract
    if (std::filesystem::exists(paths.result_path_)) {
        return read_subprocess_result(task, paths.result_path_);
    }

    // If the subprocess did not write a result file, capture the stderr output and rethrow
    std::string message = "subprocess exited with status " + std::to_string(status);
    const std::string stderr_text = read_file_prefix(paths.stderr_path_, 2000);
    if (!stderr_text.empty()) {
        message += ": " + stderr_text;
    }
    throw std::runtime_error(message);
}

// Map the stable spec backend names to the dynamic BMI backend enum
DynamicBmiBackend parse_backend(const std::string& backend) {
    if (backend == "cpp") {
        return DynamicBmiBackend::cpp;
    }
    if (backend == "fortran") {
        return DynamicBmiBackend::fortran;
    }
    throw std::runtime_error("unknown backend '" + backend + "'");
}

// Require a non-empty spec string except where empty is part of the contract
void require_non_empty_spec_string(const std::string& value,
                                   const std::string& key) {
    if (value.empty()) {
        throw std::runtime_error(
            "spec key '" + key + "' must be a non-empty string");
    }
}

// Return an empty mapping when the spec omits an explicit forcing map
const std::map<std::string, std::string>& variables_names_map_or_empty(
    const bmi::BmiSubprocessSpec& spec) {
    static const std::map<std::string, std::string> empty_variables_names_map;
    return spec.variables_names_map_ ? *spec.variables_names_map_
                                     : empty_variables_names_map;
}

// Build the forcing payload through the shared forcing contract
caf::config_value make_forcing_config_from_spec(
    const bmi::BmiSubprocessSpec& spec) {
    return make_forcing_config(*spec.forcing_file_,
                               variables_names_map_or_empty(spec));
}

// Enforce semantic constraints that JSON deserialization cannot express
void validate_spec(const bmi::BmiSubprocessSpec& spec) {
    parse_backend(spec.backend_);
    require_non_empty_spec_string(spec.plugin_path_, "plugin_path");
    require_non_empty_spec_string(spec.config_file_, "config");
    if (spec.forcing_file_) {
        make_forcing_config_from_spec(spec);
    }
    if (spec.variables_names_map_ && !spec.variables_names_map_->empty() &&
        !spec.forcing_file_) {
        throw std::runtime_error(
            "spec key 'variables_names_map' requires key 'forcing_file'");
    }
}

// Read the parent-written spec into the subprocess execution contract
bmi::BmiSubprocessSpec read_spec(const std::filesystem::path& spec_path) {
    caf::json_reader reader;
    if (!reader.load_file(spec_path.string())) {
        throw std::runtime_error("failed to parse spec file '" +
                                 spec_path.string() + "': " +
                                 caf::to_string(reader.get_error()));
    }

    bmi::BmiSubprocessSpec spec;
    if (!reader.apply(spec)) {
        throw std::runtime_error("failed to read spec file '" +
                                 spec_path.string() + "': " +
                                 caf::to_string(reader.get_error()));
    }
    validate_spec(spec);
    return spec;
}

// Write the parent-to-subprocess BMI execution contract
void write_subprocess_spec(const std::filesystem::path& spec_path,
                           DynamicBmiBackend backend,
                           const std::string& plugin_path,
                           const std::string& config_file,
                           const Task& task) {
    bmi::BmiSubprocessSpec spec;
    spec.backend_ = backend == DynamicBmiBackend::cpp ? "cpp" : "fortran";
    spec.plugin_path_ = plugin_path;
    spec.config_file_ = config_file;
    spec.output_dir_ = task.task_output_dir_.empty()
        ? ""
        : std::filesystem::absolute(task.task_output_dir_)
              .lexically_normal()
              .string();

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
        spec.forcing_file_ = *forcing_file;

        const caf::config_value::dictionary::const_iterator map_it =
            forcing_config->find("variables_names_map");
        if (map_it != forcing_config->end()) {
            const caf::config_value::dictionary* variables_names_map =
                caf::get_if<caf::config_value::dictionary>(&map_it->second);
            if (variables_names_map == nullptr) {
                throw std::runtime_error(
                    "forcing key 'variables_names_map' must be a dictionary");
            }
            spec.variables_names_map_.emplace();
            for (const std::pair<const std::string, caf::config_value>&
                     map_entry : *variables_names_map) {
                const std::string* header =
                    caf::get_if<std::string>(&map_entry.second);
                if (header == nullptr) {
                    throw std::runtime_error(
                        "forcing map entry '" + map_entry.first +
                        "' must be a string");
                }
                spec.variables_names_map_->emplace(map_entry.first, *header);
            }
        }
    }

    io::write_json_file(spec_path, spec);
}

} // namespace

// Execute a dynamic BMI model in a separate subprocess and return the result contract
TaskResult execute_dynamic_bmi_model_in_subprocess(
    DynamicBmiBackend backend,
    const std::string& plugin_path,
    const Task& task) {
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
    write_subprocess_spec(paths.spec_path_,
                          backend,
                          plugin_path,
                          *config_file,
                          task);

    return run_subprocess_task(task, paths, subprocess_path);
}

namespace bmi {

// Called by the subprocess executable (hence why it returns int instead of TaskResult)
int run_subprocess_cli(int argc, char* argv[]) {
    try {
        const std::filesystem::path spec_path = parse_subprocess_spec_path(argc, argv);
        TaskResult task_result;
        try {
            // Read the task spec from the file written by the parent process
            const BmiSubprocessSpec spec = read_spec(spec_path);

            // Rebuild the task payload expected by the shared BMI execution path
            Task task;
            task.args_.emplace_back(spec.config_file_);
            task.task_output_dir_ = spec.output_dir_;
            if (spec.forcing_file_) {
                task.args_.push_back(make_forcing_config_from_spec(spec));
            }

            // Dispatch to the requested dynamic BMI backend and execute the model as usual
            if (parse_backend(spec.backend_) == DynamicBmiBackend::cpp) {
                DynamicCppBmi model(spec.plugin_path_);
                task_result = execute_bmi_model(model, task);
            } else {
                DynamicFortranBmi model(spec.plugin_path_);
                task_result = execute_bmi_model(model, task);
            }
        } catch (...) {
            // Return a failed task result (since exceptions cannot propagate through the
            // subprocess boundary) so that it gets serialized and written to file
            task_result = make_failed_task_result(
                Task{},
                std::current_exception(),
                "BMI error");
        }
        // Write the result file that the parent process will read to complete the task
        write_subprocess_result(make_subprocess_result_path(spec_path), task_result);
        return 0;
    } catch (const std::exception& exception) {
        // If this is reached, the result-file contract may not be available. Thus write 
        // the error message to the diagnostic stderr file, which the parent process will 
        // read and include in the failed task result message.
        write_subprocess_error(exception.what());
        return 1;
    } catch (...) { 
        // Here, we have no idea what went wrong, but the absence of a result file 
        // indicates that the parent process should treat this as a failure.
        write_subprocess_error("unknown exception");
        return 1;
    }
}

} // namespace bmi

} // namespace miaow
