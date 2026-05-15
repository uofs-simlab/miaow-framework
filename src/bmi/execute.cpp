#include "miaow_bmi.hpp"

#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bmi/internal.hpp"

namespace miaow {

namespace {

// Validate the public task contract before touching files or BMI lifecycle
const bmi::BmiPipelineConfig&
require_bmi_pipeline_config_argument(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "BMI pipeline config error: task must contain exactly one "
            "top-level BMI pipeline config dictionary argument");
    }

    const bmi::BmiPipelineConfig* pipeline_config =
        caf::get_if<caf::config_value::dictionary>(&task.args_.front());
    if (pipeline_config == nullptr) {
        throw std::runtime_error(
            "BMI pipeline config error: first task argument must be a "
            "top-level BMI pipeline config dictionary");
    }
    return *pipeline_config;
}

// Preserve useful domain context while normalizing generic BMI failures
std::string format_bmi_error_message(const std::string& error_message) {
    const bool has_known_prefix =
        error_message.rfind("Forcing config error: ", 0) == 0 ||
        error_message.rfind("Forcing data error: ", 0) == 0 ||
        error_message.rfind("Forcing alignment error: ", 0) == 0 ||
        error_message.rfind("BMI coupling error: ", 0) == 0 ||
        error_message.rfind("BMI forcing error: ", 0) == 0 ||
        error_message.rfind("BMI pipeline config error: ", 0) == 0 ||
        error_message.rfind("BMI pipeline error: ", 0) == 0;
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

// Run the standard BMI lifecycle and collect runtime artifacts
TaskResult execute_bmi_lifecycle(bmi_api::Bmi& model,
                                 const std::string& config_path,
                                 const Task& task,
                                 std::vector<std::string> artifact_paths) {
    bool needs_finalize = false;
    std::size_t update_steps = 0;

    try {
        // Initialize before opening output so BMI variable metadata is available
        needs_finalize = true;
        model.Initialize(config_path);

        {
            // Open one CSV output file if an output directory exists
            std::unique_ptr<bmi::OutputFile> output_file;
            if (!task.task_output_dir_.empty()) {
                output_file = std::make_unique<bmi::OutputFile>(
                    bmi::open_output_file(model,
                                          task.task_output_dir_,
                                          task.task_output_name_));
                artifact_paths.push_back(output_file->artifact_path_);
            }

            // Update until the selected BMI clock reaches its configured end time
            const double end_time = model.GetEndTime();
            update_steps = 0;
            while (model.GetCurrentTime() < end_time) {
                model.Update();
                ++update_steps;

                const double current_time = model.GetCurrentTime();
                if (output_file) {
                    bmi::write_output_step(model, *output_file, current_time);
                }
            }
        }

        // Finalize after output streams have been closed
        needs_finalize = false;
        model.Finalize();

    // Handle exceptions thrown during initialization, update, or finalization
    } catch (...) {
        std::string failure_message =
            format_bmi_error_message(std::current_exception()) +
            " (updates: " + std::to_string(update_steps) + ")";

        // Try to finalize a model that failed after successful initialization
        if (needs_finalize) {
            try {
                model.Finalize();
            } catch (const std::exception& finalize_exception) {
                failure_message +=
                    std::string(" | BMI finalize error: ") +
                    finalize_exception.what();
            } catch (...) {
                failure_message += " | BMI finalize error: unknown exception";
            }
        }
        throw std::runtime_error(failure_message);
    }

    std::ostringstream message;
    message << "BMI workflow complete (updates: " << update_steps << ")";
    return TaskResult{TaskStatus::succeeded, message.str(), std::move(artifact_paths)};
}

} // namespace

TaskResult execute_bmi_pipeline(
    const Task& task,
    std::unique_ptr<bmi_api::Bmi> supplied_model) {
    // Read the top-level pipeline config argument from the task
    const bmi::BmiPipelineConfig& pipeline_config =
        require_bmi_pipeline_config_argument(task);

    // Persist the pipeline config because BMI Initialize accepts a file path
    const std::filesystem::path pipeline_config_path =
        bmi::bmi_pipeline_config_file_path(task);
    std::vector<std::string> artifact_paths =
        bmi::bmi_pipeline_config_artifact_paths(task);
    bmi::write_bmi_pipeline_config_file(pipeline_config, pipeline_config_path);

    // Wrap the supplied model in the pipeline adapter and execute it as BMI
    bmi::BmiPipeline pipeline(std::move(supplied_model));
    return execute_bmi_lifecycle(pipeline,
                                 pipeline_config_path.string(),
                                 task,
                                 std::move(artifact_paths));
}

TaskResult BmiPipelineSimulationUnit::execute(const Task& task) {
    try {
        if (execution_mode_ != BmiExecutionMode::in_process) {
            throw std::runtime_error(
                "BMI pipeline error: subprocess execution is not supported "
                "for BMI pipeline configs");
        }
        return execute_bmi_pipeline(task);
    } catch (...) {
        return make_failed_task_result(std::current_exception(), "BMI error");
    }
}

} // namespace miaow
