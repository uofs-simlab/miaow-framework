#pragma once

#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// To avoid confusion with the miaow::bmi namespace, we alias the BMI API namespace here
namespace bmi_api = ::bmi;

namespace bmi {

// Execute update loop and optional forcing for one initialized BMI model
std::size_t run_model(
    bmi_api::Bmi& model,
    const std::optional<std::string>& forcing_config_path,
    const std::string& task_output_dir);

} // namespace bmi

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from bmi_api::Bmi.
 */
template <class BmiModelType>
class BmiSimulationUnit : public SimulationUnit {
public:
    static_assert(std::is_base_of_v<bmi_api::Bmi, BmiModelType>,
                  "BmiModelType must derive from bmi::Bmi");
    static_assert(std::is_default_constructible_v<BmiModelType>,
                  "BmiModelType must be default constructible");

    /**
     * @brief Execute one BMI-wrapped simulation.
     * @param task Task arguments where args[0] is the BMI config path and
     *             args[1] optionally points to a forcing config file.
     * @return Structured task result for this execution.
     */
    TaskResult execute(const Task& task) override {
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

        BmiModelType bmi_model;

        bool model_initialized = false;
        std::size_t update_steps = 0;

        try {
            bmi_model.Initialize(*config_file);
            model_initialized = true;

            update_steps = bmi::run_model(
                bmi_model, forcing_config_path, task.task_output_dir_);

            bmi_model.Finalize();
            model_initialized = false;
        } catch (const std::exception& exception) {
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            task_result.status_ = TaskStatus::failed;
            const std::string error_message = exception.what();
            const bool has_known_prefix =
                error_message.rfind("Forcing config error: ", 0) == 0 ||
                error_message.rfind("Forcing data error: ", 0) == 0 ||
                error_message.rfind("Forcing alignment error: ", 0) == 0 ||
                error_message.rfind("BMI forcing error: ", 0) == 0;
            if (has_known_prefix) {
                task_result.message_ = error_message;
            } else {
                task_result.message_ = std::string("BMI error: ") + error_message;
            }
            return task_result;
        } catch (...) {
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = "BMI error: unknown exception";
            return task_result;
        }

        std::ostringstream message;
        message << "BMI workflow complete (updates: " << update_steps << ")";
        task_result.status_ = TaskStatus::succeeded;
        task_result.message_ = message.str();
        return task_result;
    }
};

} // namespace miaow
