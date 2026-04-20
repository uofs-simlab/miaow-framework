#include "miaow_bmi.hpp"

#include <exception>
#include <optional>
#include <sstream>
#include <string>

#include "bmi/internal.hpp"

namespace miaow {

namespace {

std::string format_bmi_error_message(const std::string& error_message) {
    // Preserve forcing-prefixed errors and prefix only generic BMI failures
    const bool has_known_prefix =
        error_message.rfind("Forcing config error: ", 0) == 0 ||
        error_message.rfind("Forcing data error: ", 0) == 0 ||
        error_message.rfind("Forcing alignment error: ", 0) == 0 ||
        error_message.rfind("BMI forcing error: ", 0) == 0;
    if (has_known_prefix) {
        return error_message;
    }
    return std::string("BMI error: ") + error_message;
}

} // namespace

TaskResult execute_bmi_model(bmi_api::Bmi& model, const Task& task) {
    TaskResult task_result;
    task_result.simulation_unit_id_ = task.simulation_unit_id_;

    if (task.args_.empty()) {
        task_result.status_ = TaskStatus::failed;
        task_result.message_ = "BMI error: missing config path in task specification";
        return task_result;
    }

    const std::string* config_file = caf::get_if<std::string>(&task.args_.front());
    if (!config_file) {
        task_result.status_ = TaskStatus::failed;
        task_result.message_ =
            "BMI error: first task argument must be a config path string";
        return task_result;
    }

    std::optional<std::string> forcing_config_path;
    if (task.args_.size() >= 2) {
        // args[1] is optional forcing config path for external forcing injection
        const std::string* forcing_config_file =
            caf::get_if<std::string>(&task.args_.at(1));
        if (!forcing_config_file) {
            task_result.status_ = TaskStatus::failed;
            task_result.message_ =
                "Forcing config error: second task argument must be a forcing "
                "config path string";
            return task_result;
        }
        forcing_config_path = *forcing_config_file;
    }

    bool needs_finalize = false;
    std::size_t update_steps = 0;

    try {
        model.Initialize(*config_file);
        needs_finalize = true;

        bmi::run_model(
            model, forcing_config_path, task.task_output_dir_, update_steps);

        needs_finalize = false;
        model.Finalize();
    } catch (const std::exception& exception) {
        const std::string base_message = format_bmi_error_message(exception.what()) +
            " (updates: " + std::to_string(update_steps) + ")";
        if (needs_finalize) {
            needs_finalize = false;
            try {
                model.Finalize();
            } catch (const std::exception& finalize_exception) {
                task_result.status_ = TaskStatus::failed;
                task_result.message_ = base_message +
                    " | BMI finalize error: " + finalize_exception.what();
                return task_result;
            } catch (...) {
                task_result.status_ = TaskStatus::failed;
                task_result.message_ = base_message +
                    " | BMI finalize error: unknown exception";
                return task_result;
            }
        }
        task_result.status_ = TaskStatus::failed;
        task_result.message_ = base_message;
        return task_result;
    } catch (...) {
        const std::string base_message = std::string("BMI error: unknown exception") +
            " (updates: " + std::to_string(update_steps) + ")";
        if (needs_finalize) {
            needs_finalize = false;
            try {
                model.Finalize();
            } catch (const std::exception& finalize_exception) {
                task_result.status_ = TaskStatus::failed;
                task_result.message_ = base_message +
                    " | BMI finalize error: " + finalize_exception.what();
                return task_result;
            } catch (...) {
                task_result.status_ = TaskStatus::failed;
                task_result.message_ = base_message +
                    " | BMI finalize error: unknown exception";
                return task_result;
            }
        }
        task_result.status_ = TaskStatus::failed;
        task_result.message_ = base_message;
        return task_result;
    }

    std::ostringstream message;
    message << "BMI workflow complete (updates: " << update_steps << ")";
    task_result.status_ = TaskStatus::succeeded;
    task_result.message_ = message.str();
    return task_result;
}

} // namespace miaow
