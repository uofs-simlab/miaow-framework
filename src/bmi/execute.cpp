#include "miaow_bmi.hpp"

#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bmi/internal.hpp"

namespace miaow {

namespace {

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
            // Open one CSV output file when storage and output are enabled
            std::unique_ptr<bmi::OutputFile> output_file;
            const std::size_t output_interval_steps = this->output_interval_steps();
            const std::vector<std::string>& selected_output_variables =
                output_variables();
            if (!task.task_output_dir_.empty() && !selected_output_variables.empty()) {
                // Output variable selection comes from the initialized pipeline config
                output_file = std::make_unique<bmi::OutputFile>(
                    bmi::open_output_file(*this, task.task_output_dir_,
                                          task.task_name_, selected_output_variables));
                runtime_artifact_paths.push_back(output_file->artifact_path_);
            }

            // Update until the selected BMI clock reaches its configured end time
            const double end_time = GetEndTime();
            double current_time = GetCurrentTime();
            while (current_time < end_time) {
                // Update each model module and advance the forcing stream
                Update();

                // Update the time step count and model clock for the next iteration
                ++update_steps;
                current_time = GetCurrentTime();

                // Positive intervals write every configured number of updates
                if (output_file && output_interval_steps > 0 &&
                    update_steps % output_interval_steps == 0) {
                    bmi::write_output_step(*this, *output_file, current_time);
                }
            }

            // Write a final row unless the final update already produced one
            const bool final_state_already_written =
                output_interval_steps > 0 && update_steps > 0 &&
                update_steps % output_interval_steps == 0;
            if (output_file && !final_state_already_written) {
                bmi::write_output_step(*this, *output_file, current_time);
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

} // namespace miaow
