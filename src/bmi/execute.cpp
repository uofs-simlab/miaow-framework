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

// The forcing is provided as a double; convert to BMI model type if necessary
void set_forcing_value(bmi_api::Bmi& model,
                       const std::string& name,
                       double raw_value) {
    const std::string type = model.GetVarType(name);
    if (type == "double") {
        model.SetValue(name, &raw_value);
        return;
    }
    if (type == "float") {
        float value = static_cast<float>(raw_value);
        model.SetValue(name, &value);
        return;
    }
    throw std::runtime_error("BMI forcing error: unsupported BMI variable type '" +
                             type + "' for input variable '" + name + "'");
}

// Advance the model and forcing in lockstep and write CSV output
void run_model(
    bmi_api::Bmi& model,
    const caf::config_value::dictionary* forcing_config,
    const std::string& task_output_dir,
    const std::string& task_output_name,
    std::size_t& update_steps,
    std::vector<std::string>& artifact_paths) {

    // Build the forcing source once so the update loop only moves scalar values
    std::unique_ptr<bmi_api::Bmi> forcing_model;
    std::vector<std::string> forcing_vars;
    if (forcing_config != nullptr) {
        forcing_model = bmi::make_forcing_bmi(*forcing_config, model);
        forcing_vars = forcing_model->GetOutputVarNames();
    }

    // Open one CSV output file if an output directory exists
    std::unique_ptr<bmi::OutputFile> output_file;
    if (!task_output_dir.empty()) {
        output_file = std::make_unique<bmi::OutputFile>(
            bmi::open_output_file(model, task_output_dir, task_output_name));
        artifact_paths.push_back(output_file->artifact_path_);
    }

    // Get the model end time for update loop condition and initialize update step counter
    const double end_time = model.GetEndTime();
    update_steps = 0;

    // Main iteration loop: update the model and forcing until the end time is reached
    while (model.GetCurrentTime() < end_time) {
        if (forcing_model) {
            // If there is forcing, apply to each variable requested by the model
            for (const std::string& forcing_var : forcing_vars) {
                double forcing_value = 0.0;
                forcing_model->GetValue(forcing_var, &forcing_value);
                set_forcing_value(model, forcing_var, forcing_value);
            }
            // Advance the forcing model to the next time step
            forcing_model->Update();
        }

        // Call the model update function and increment the update step counter
        model.Update();
        ++update_steps;

        // Get the current model time and write one output row
        const double current_time = model.GetCurrentTime();
        if (output_file) {
            bmi::write_output_step(model, *output_file, current_time);
        }
    }

    // Finalize the forcing model if it exists
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

// Convert a captured exception into the BMI task result message
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

TaskResult execute_bmi_model(bmi_api::Bmi& model, const Task& task) {
    if (task.args_.empty()) {
        throw std::runtime_error("missing config path in task specification");
    }

    // Require the first task argument to be the BMI config path
    const std::string* config_file = caf::get_if<std::string>(&task.args_.front());
    if (!config_file) {
        throw std::runtime_error("first task argument must be a config path string");
    }

    const caf::config_value::dictionary* forcing_config = nullptr;
    if (task.args_.size() >= 2) {
        // args[1] is the structured forcing payload when external forcing is enabled
        forcing_config =
            caf::get_if<caf::config_value::dictionary>(&task.args_.at(1));
        if (!forcing_config) {
            throw std::runtime_error(
                "Forcing config error: second task argument must be a forcing "
                "dictionary");
        }
    }

    bool needs_finalize = false;
    std::size_t update_steps = 0;
    std::vector<std::string> artifact_paths;

    // Main execution workflow
    try {
        // The model handles reading the config file
        model.Initialize(*config_file);
        needs_finalize = true;

        // MIAOW applies the forcing and advances the model in lockstep
        run_model(model,
                  forcing_config,
                  task.task_output_dir_,
                  task.task_output_name_,
                  update_steps,
                  artifact_paths);

        // Finalize the model after successful completion of the update loop
        needs_finalize = false;
        model.Finalize();

    // Handle exceptions thrown during model initialization, update, or finalization
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

} // namespace miaow
