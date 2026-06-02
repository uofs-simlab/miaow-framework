#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "io/internal.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <caf/all.hpp>
#include <caf/config_value.hpp>
#include <caf/ipv4_endpoint.hpp>
#include <caf/ipv4_subnet.hpp>
#include <caf/ipv6_endpoint.hpp>
#include <caf/ipv6_subnet.hpp>
#include <caf/json.hpp>

namespace miaow {

namespace {

constexpr const char* bmi_subprocess_error_prefix = "BMI subprocess error";
// CAF config values require core meta objects before JSON IO
void initialize_caf() {
    caf::init_global_meta_objects<caf::id_block::core_module>();
}

// Parse the subprocess executable command-line contract
std::filesystem::path parse_subprocess_task_path(int argc, char* argv[]) {
    std::string task_path;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--task") {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + argv[index]);
            }
            ++index;
            task_path = argv[index];
        } else {
            throw std::runtime_error("unknown argument '" + arg + "'");
        }
    }

    if (task_path.empty()) {
        throw std::runtime_error("missing --task");
    }
    return std::filesystem::absolute(task_path).lexically_normal();
}

// Write child-process setup failures through the diagnostic stderr file
void write_subprocess_error(const std::string& message) {
    std::cerr << bmi_subprocess_error_prefix << ": subprocess setup error: " << message
              << "\n";
}

// Read the parent-written JSON file into the subprocess task payload
Task read_subprocess_task(const std::filesystem::path& task_path) {
    caf::json_reader reader;
    if (!reader.load_file(task_path.string())) {
        throw std::runtime_error("failed to parse task file '" + task_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }

    Task task;
    if (!reader.apply(task)) {
        throw std::runtime_error("failed to read task file '" + task_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }

    // Validate constraints that JSON deserialization cannot express
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "subprocess task must contain exactly one top-level BMI pipeline "
            "config argument");
    }
    if (caf::get_if<caf::config_value::dictionary>(&task.args_.front()) == nullptr) {
        throw std::runtime_error(
            "subprocess task argument must be a BmiPipelineConfig");
    }
    if (!task.task_output_dir_.empty() && task.task_output_name_.empty()) {
        throw std::runtime_error(
            "subprocess task key 'task_output_name' must be a non-empty string");
    }
    return task;
}

} // namespace

} // namespace miaow

// Entry point for subprocess execution launched via Boost.Process
int main(int argc, char* argv[]) {
    try {
        miaow::initialize_caf();
        const std::filesystem::path task_path =
            miaow::parse_subprocess_task_path(argc, argv);
        miaow::TaskResult task_result;
        try {
            // Read the subprocess task payload written by the parent process
            const miaow::Task task = miaow::read_subprocess_task(task_path);

            // Config-constructible pipelines build all models in the child process
            miaow::BmiPipeline pipeline(task);
            task_result = pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions cannot propagate through the subprocess boundary
            task_result =
                miaow::make_failed_task_result(std::current_exception(), "BMI error");
        }

        // Write the result file that the parent process will read
        const std::string task_filename = task_path.filename().string();
        const std::string input_suffix = "_input.json";
        std::filesystem::path result_path =
            task_path.parent_path() / (task_path.stem().string() + "_result.json");
        if (task_filename.size() > input_suffix.size() &&
            task_filename.ends_with(input_suffix)) {
            const std::string result_stem =
                task_filename.substr(0, task_filename.size() - input_suffix.size());
            result_path = task_path.parent_path() / (result_stem + "_result.json");
        }
        miaow::io::write_json_file(result_path, task_result, false);
        return 0;
    } catch (const std::exception& exception) {
        // The result-file contract may not be available at this point
        miaow::write_subprocess_error(exception.what());
        return 1;
    } catch (...) {
        // The absence of a result file tells the parent process that setup failed
        miaow::write_subprocess_error("unknown exception");
        return 1;
    }
}
