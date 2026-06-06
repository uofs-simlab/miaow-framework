#include "miaow_bmi.hpp"

#include <cstdlib>
#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <caf/actor_system.hpp>
#include <caf/config_value.hpp>

#include "bmi/internal.hpp"
#include "core/internal.hpp"

namespace miaow {

namespace {

#ifndef MIAOW_SUBPROCESS_PATH
#define MIAOW_SUBPROCESS_PATH ""
#endif

// Preserve useful domain context while normalizing generic BMI failures
std::string format_bmi_error_message(const std::string& error_message) {
    const bool has_known_prefix =
        error_message.starts_with("Forcing config error: ") ||
        error_message.starts_with("Forcing data error: ") ||
        error_message.starts_with("Forcing alignment error: ") ||
        error_message.starts_with("BMI coupling error: ") ||
        error_message.starts_with("BMI forcing error: ") ||
        error_message.starts_with("BMI pipeline config error: ") ||
        error_message.starts_with("BMI pipeline error: ");
    if (has_known_prefix) {
        return error_message;
    }
    return std::string("BMI error: ") + error_message;
}

// Convert captured exceptions into task result messages
std::string format_bmi_error_message(std::exception_ptr exception) {
    if (!exception) {
        return "BMI error: unknown exception";
    }

    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& caught_exception) {
        return format_bmi_error_message(caught_exception.what());
    } catch (...) {
        return "BMI error: unknown exception";
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

// Validate constraints that actor deserialization cannot express
void validate_bmi_subprocess_task(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "subprocess task must contain exactly one top-level BMI pipeline "
            "config argument");
    }
    if (caf::get_if<caf::config_value::dictionary>(&task.args_.front()) == nullptr) {
        throw std::runtime_error("subprocess task argument must be a BmiPipelineConfig");
    }
}

} // namespace

TaskResult BmiPipeline::execute_bmi_lifecycle(const Task& task) {
    std::vector<std::string> runtime_artifact_paths = artifact_paths();
    bool needs_finalize = false;
    std::size_t update_steps = 0;

    try {
        // Initialize before opening output so BMI variable metadata is available
        needs_finalize = true;
        Initialize(config_path_string());

        {
            // Open one CSV output file if an output directory exists
            std::unique_ptr<bmi::OutputFile> output_file;
            if (!task.task_output_dir_.empty()) {
                // Output variable selection comes from the initialized pipeline config
                output_file = std::make_unique<bmi::OutputFile>(
                    bmi::open_output_file(*this, task.task_output_dir_,
                                          task.task_name_, output_variables()));
                runtime_artifact_paths.push_back(output_file->artifact_path_);
            }

            // Update until the selected BMI clock reaches its configured end time
            const double end_time = GetEndTime();
            update_steps = 0;
            while (GetCurrentTime() < end_time) {
                // Update each model module and advance the forcing stream
                Update();
                ++update_steps;

                const double current_time = GetCurrentTime();
                if (output_file) {
                    bmi::write_output_step(*this, *output_file, current_time);
                }
            }
        }

        // Finalize after output streams have been closed
        needs_finalize = false;
        Finalize();

        // Handle exceptions thrown during initialization, update, or finalization
    } catch (...) {
        std::string failure_message = format_bmi_error_message(std::current_exception()) +
                                      " (updates: " + std::to_string(update_steps) + ")";

        // Try to finalize a model that failed after successful initialization
        if (needs_finalize) {
            try {
                Finalize();
            } catch (const std::exception& finalize_exception) {
                failure_message +=
                    std::string(" | BMI finalize error: ") + finalize_exception.what();
            } catch (...) {
                failure_message += " | BMI finalize error: unknown exception";
            }
        }
        throw std::runtime_error(failure_message);
    }

    std::ostringstream message;
    message << "BMI workflow complete (updates: " << update_steps << ")";
    return TaskResult{TaskStatus::succeeded, message.str(),
                      std::move(runtime_artifact_paths)};
}

namespace bmi {

TaskResult execute_bmi_lifecycle_in_subprocess(caf::actor_system& actor_system,
                                               const Task& task) {
    validate_bmi_subprocess_task(task);

    // Resolve the runner path before delegating to generic subprocess execution
    const std::string subprocess_path = configured_subprocess_path();
    return core::execute_single_task_subprocess(actor_system, task, subprocess_path);
}

} // namespace bmi

} // namespace miaow
