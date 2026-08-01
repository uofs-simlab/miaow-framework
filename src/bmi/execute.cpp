#include "miaow_bmi.hpp"

#include <cmath>
#include <exception>
#include <memory>
#include <optional>
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
    std::vector<std::string> runtime_artifact_paths;
    if (std::optional<std::string> config_artifact =
            bmi::write_bmi_pipeline_config_artifact(task)) {
        runtime_artifact_paths.push_back(std::move(*config_artifact));
    }
    bool needs_finalize = false;

    try {
        // Initialize before opening output so BMI variable metadata is available
        needs_finalize = true;
        initialize_task();

        {
            // Get the output configuration
            std::unique_ptr<bmi::OutputFile> output_file;
            const std::optional<double>& configured_output_interval = output_interval();
            const double time_step_tolerance = this->time_step_tolerance();
            const std::vector<std::string>& selected_output_variables = output_variables();

            // If there are output variables and an output directory, open the output file
            if (!task.task_output_dir_.empty() && !selected_output_variables.empty()) {
                output_file = std::make_unique<bmi::OutputFile>(
                    bmi::open_output_file(*this, task.task_output_dir_,
                                          task.task_name_, selected_output_variables));
                runtime_artifact_paths.push_back(output_file->artifact_path_);
            }

            // The selected output model clock governs all schedule boundaries
            const double end_time = GetEndTime();
            double current_time = GetCurrentTime();
            if (end_time - current_time < -time_step_tolerance) {
                throw std::runtime_error(
                    "BMI pipeline error: current time is later than the end time");
            }

            if (!output_file) {
                // If there is no output file, just run the model to the end time
                UpdateUntil(end_time);
            } else {
                // Write output states and remember the last reported model time
                double last_output_time = current_time;
                const auto write_current_state = [&]() {
                    current_time = GetCurrentTime();
                    bmi::write_output_step(*this, *output_file, current_time);
                    last_output_time = current_time;
                };
                write_current_state();

                if (!configured_output_interval) {
                    // An absent interval writes every completed update
                    double remaining_time = end_time - current_time;
                    while (remaining_time > time_step_tolerance) {
                        Update();
                        current_time = GetCurrentTime();
                        remaining_time = end_time - current_time;
                        if (remaining_time < -time_step_tolerance) {
                            throw std::runtime_error(
                                "BMI pipeline error: update passed the end time");
                        }
                        write_current_state();
                    }
                } else {
                    // A positive interval writes reachable model-time boundaries
                    const double start_time = current_time;
                    std::size_t output_index = 1;
                    double target_time = start_time + static_cast<double>(output_index) *
                                         *configured_output_interval;

                    while (end_time - target_time > time_step_tolerance) {
                        UpdateUntil(target_time);
                        write_current_state();
                        ++output_index;
                        target_time = start_time + static_cast<double>(output_index) *
                                        *configured_output_interval;
                    }

                    // Advance to the end time without duplicating a coincident boundary
                    UpdateUntil(end_time);
                    current_time = GetCurrentTime();
                    if (std::abs(current_time - last_output_time) >
                        time_step_tolerance) {
                        write_current_state();
                    }
                }
            }
        }

        // Finalize after output streams have been closed
        needs_finalize = false;
        Finalize();

        // Handle exceptions thrown during initialization, update, or finalization
    } catch (...) {
        const std::size_t update_steps = completed_update_count();
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
    message << "BMI workflow complete (updates: " << completed_update_count() << ")";
    return TaskResult{TaskStatus::succeeded, message.str(),
                      std::move(runtime_artifact_paths)};
}

} // namespace miaow
