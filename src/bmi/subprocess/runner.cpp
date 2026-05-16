#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "bmi/internal.hpp"
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
std::filesystem::path parse_subprocess_spec_path(int argc, char* argv[]) {
    std::string spec_path;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--spec") {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + argv[index]);
            }
            ++index;
            spec_path = argv[index];
        } else {
            throw std::runtime_error("unknown argument '" + arg + "'");
        }
    }

    if (spec_path.empty()) {
        throw std::runtime_error("missing --spec");
    }
    return std::filesystem::absolute(spec_path).lexically_normal();
}

// Write the complete result contract consumed by the parent process
void write_subprocess_result(const std::filesystem::path& result_path,
                             const TaskResult& task_result) {
    io::write_json_file(result_path, task_result, false);
}

// Write child-process setup failures through the diagnostic stderr file
void write_subprocess_error(const std::string& message) {
    std::cerr << bmi_subprocess_error_prefix << ": subprocess setup error: " << message
              << "\n";
}

// Require a non-empty task payload string except where empty is part of the contract
void require_non_empty_task_string(const std::string& value, const std::string& key) {
    if (value.empty()) {
        throw std::runtime_error("subprocess task key '" + key +
                                 "' must be a non-empty string");
    }
}

// Enforce semantic constraints that JSON deserialization cannot express
void validate_subprocess_task(const bmi::BmiSubprocessTaskSpec& subprocess_task) {
    if (subprocess_task.pipeline_config_.empty()) {
        throw std::runtime_error(
            "subprocess task key 'pipeline_config' must be a non-empty "
            "dictionary");
    }
    if (!subprocess_task.output_dir_.empty()) {
        require_non_empty_task_string(subprocess_task.output_name_, "output_name");
    }
}

// Read the parent-written JSON file into the subprocess task payload
bmi::BmiSubprocessTaskSpec read_subprocess_task(const std::filesystem::path& spec_path) {
    caf::json_reader reader;
    if (!reader.load_file(spec_path.string())) {
        throw std::runtime_error("failed to parse spec file '" + spec_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }

    bmi::BmiSubprocessTaskSpec subprocess_task;
    if (!reader.apply(subprocess_task)) {
        throw std::runtime_error("failed to read spec file '" + spec_path.string() +
                                 "': " + caf::to_string(reader.get_error()));
    }
    validate_subprocess_task(subprocess_task);
    return subprocess_task;
}

} // namespace

} // namespace miaow

// Entry point for subprocess execution launched via Boost.Process
int main(int argc, char* argv[]) {
    try {
        miaow::initialize_caf();
        const std::filesystem::path spec_path =
            miaow::parse_subprocess_spec_path(argc, argv);
        miaow::TaskResult task_result;
        try {
            // Read the subprocess task payload written by the parent process
            const miaow::bmi::BmiSubprocessTaskSpec subprocess_task =
                miaow::read_subprocess_task(spec_path);

            // Rebuild the task payload expected by the shared BMI execution path
            miaow::Task task;
            task.args_.push_back(caf::config_value{subprocess_task.pipeline_config_});
            task.task_output_dir_ = subprocess_task.output_dir_;
            task.task_output_name_ = subprocess_task.output_name_;

            // Config-constructible pipelines build all models in the child process
            miaow::BmiPipeline pipeline(task);
            task_result = pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions cannot propagate through the subprocess boundary
            task_result =
                miaow::make_failed_task_result(std::current_exception(), "BMI error");
        }

        // Write the result file that the parent process will read
        miaow::write_subprocess_result(miaow::bmi::make_subprocess_result_path(spec_path),
                                       task_result);
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
