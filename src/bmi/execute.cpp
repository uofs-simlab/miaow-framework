#include "miaow_bmi.hpp"

#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "bmi/internal.hpp"

namespace miaow {

namespace {

void run_model(
    bmi_api::Bmi& model, // BMI model, init'd with model config file in execute_bmi_model
    const caf::config_value::dictionary* forcing_config,
    const std::string& task_output_dir,
    std::size_t& update_steps /* diagnostic to determine number of update steps */) {

    // Build the forcing source once so the update loop only moves scalar values
    std::unique_ptr<bmi_api::Bmi> forcing_model;
    std::vector<std::string> forcing_vars;
    if (forcing_config != nullptr) {
        forcing_model = bmi::make_forcing_bmi(*forcing_config, model);
        forcing_vars = forcing_model->GetOutputVarNames();
    }

    // Open output streams for BMI output variables if an output directory is provided
    std::vector<bmi::OutputVariable> output_vars;
    if (!task_output_dir.empty()) {
        output_vars = bmi::open_output_vars(model, task_output_dir);
    }

    // Get the model end time for update loop condition and initialize update step counter
    const double end_time = model.GetEndTime();
    update_steps = 0;

    // Main execution loop: update the model and forcing until the end time is reached
    while (model.GetCurrentTime() < end_time) {
        if (forcing_model) {
            // If there is forcing, apply to each variable requested by the model
            for (const std::string& forcing_var : forcing_vars) {
                double forcing_value = 0.0;
                forcing_model->GetValue(forcing_var, &forcing_value);
                model.SetValue(forcing_var, &forcing_value);
            }
            forcing_model->Update(); // advance the forcing model to the next time step
        }

        // Call the model update function and increment the update step counter
        model.Update();
        ++update_steps;

        // Get the current model time and write output variable values
        const double current_time = model.GetCurrentTime();
        for (bmi::OutputVariable& variable : output_vars) {
            bmi::write_output_step(model, variable, current_time);
        }
    }

    if (forcing_model) {
        forcing_model->Finalize();
    }
}

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

    const caf::config_value::dictionary* forcing_config = nullptr;
    if (task.args_.size() >= 2) {
        // args[1] is the structured forcing payload when external forcing is enabled
        forcing_config =
            caf::get_if<caf::config_value::dictionary>(&task.args_.at(1));
        if (!forcing_config) {
            task_result.status_ = TaskStatus::failed;
            task_result.message_ =
                "Forcing config error: second task argument must be a forcing "
                "dictionary";
            return task_result;
        }
    }

    bool needs_finalize = false;
    std::size_t update_steps = 0;

    try {
        model.Initialize(*config_file);
        needs_finalize = true;

        run_model(model, forcing_config, task.task_output_dir_, update_steps);

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
