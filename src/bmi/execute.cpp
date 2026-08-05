#include "bmi/internal.hpp"

#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
        error_message.starts_with("BMI output error: ") ||
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

TaskResult bmi::BmiPipeline::execute_bmi_lifecycle(const Task& task) {
    const BmiPipelineConfig pipeline_config(task);
    std::vector<std::string> runtime_artifact_paths;
    if (std::optional<std::string> config_artifact =
            bmi::write_bmi_pipeline_config_artifact(task)) {
        runtime_artifact_paths.push_back(std::move(*config_artifact));
    }
    try {
        // Initialize before opening output so BMI variable metadata is available
        initialize(pipeline_config);

        {
            // Get the output configuration
            std::unique_ptr<bmi::OutputWriter> output_writer;
            const std::optional<double>& configured_output_interval = output_interval_;

            // Schedule comparisons use the time unit exposed by the pipeline
            const double time_step_tolerance = time_step_tolerance_;
            const std::vector<std::string>& selected_output_variables = output_variables_;
            ::bmi::Bmi& selected_output_model = output_model();

            // Construct the selected writer only when task output is enabled
            if (!task.task_output_dir_.empty() && !selected_output_variables.empty()) {
                output_writer =
                    std::make_unique<bmi::CsvOutputWriter>(selected_output_model,
                                                           task.task_output_dir_,
                                                           task.task_name_,
                                                           selected_output_variables);
                runtime_artifact_paths.push_back(output_writer->artifact_path());
            }

            // The selected output model clock governs all schedule boundaries
            const double end_time = selected_output_model.GetEndTime();
            double current_time = selected_output_model.GetCurrentTime();
            if (end_time - current_time < -time_step_tolerance) {
                throw std::runtime_error(
                    "BMI pipeline error: current time is later than the end time");
            }

            if (!output_writer) {
                // Run directly to the end time when task output is disabled
                update_until(end_time);
            } else {
                // Write output states and remember the last reported model time
                double last_output_time = current_time;
                const auto write_current_state = [&]() {
                    current_time = selected_output_model.GetCurrentTime();
                    output_writer->write_step(selected_output_model, current_time);
                    last_output_time = current_time;
                };
                write_current_state();

                if (!configured_output_interval) {
                    // An absent interval writes every completed update
                    double remaining_time = end_time - current_time;
                    while (remaining_time > time_step_tolerance) {
                        update();
                        current_time = selected_output_model.GetCurrentTime();
                        remaining_time = end_time - current_time;
                        if (remaining_time < -time_step_tolerance) {
                            throw std::runtime_error(
                                "BMI pipeline error: update passed the end time");
                        }
                        write_current_state();
                    }
                } else {
                    // A positive interval requests output at successive model-time
                    // boundaries
                    const double start_time = current_time;
                    std::size_t output_index = 1;
                    double target_time = start_time + static_cast<double>(output_index) *
                                                          *configured_output_interval;

                    while (end_time - target_time > time_step_tolerance) {
                        update_until(target_time);
                        write_current_state();
                        ++output_index;
                        target_time = start_time + static_cast<double>(output_index) *
                                                       *configured_output_interval;
                    }

                    // Advance to the end time without duplicating a coincident boundary
                    update_until(end_time);
                    current_time = selected_output_model.GetCurrentTime();
                    if (std::abs(current_time - last_output_time) > time_step_tolerance) {
                        write_current_state();
                    }
                }
            }
        }

        // Finalize after output streams have been closed
        finalize();

        // Handle exceptions thrown during initialization, update, or finalization
    } catch (...) {
        const std::size_t update_steps = completed_update_count_;
        std::string failure_message = format_bmi_error_message(std::current_exception()) +
                                      " (updates: " + std::to_string(update_steps) + ")";

        // Try to finalize a model that failed after successful initialization
        if (initialized_module_count_ > 0) {
            try {
                finalize();
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
    message << "BMI workflow complete (updates: " << completed_update_count_ << ")";
    return TaskResult{TaskStatus::succeeded, message.str(),
                      std::move(runtime_artifact_paths)};
}

namespace static_bmi_execution {

TaskResult execute_task(const Task& task, std::unique_ptr<bmi_api::Bmi> model) {
    bmi::BmiPipeline pipeline(std::move(model));
    return pipeline.execute_bmi_lifecycle(task);
}

} // namespace static_bmi_execution

TaskResult DynamicBmiSimulationUnit::execute(const Task& task, ExecutionContext& context) {
    // Subprocess modes run the serialized BMI pipeline in the subprocess
    if (task.execution_mode_ == ExecutionMode::single_task_subprocess ||
        task.execution_mode_ == ExecutionMode::persistent_subprocess) {
        return context.execute_subprocess(task);
    }

    try {
        // Construct all configured model modules locally for an in-process task
        bmi::BmiPipeline pipeline;
        return pipeline.execute_bmi_lifecycle(task);
    } catch (...) {
        // Exceptions become failed task results so the job can keep running
        return make_failed_task_result(std::current_exception(), "BMI error");
    }
}

} // namespace miaow
