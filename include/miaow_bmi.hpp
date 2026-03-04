#pragma once

#include <cstddef>
#include <exception>
#include <sstream>
#include <string>
#include <type_traits>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from bmi::Bmi.
 */
template <class BmiModelType>
class BmiSimulationUnit : public SimulationUnit {
public:
    static_assert(std::is_base_of_v<bmi::Bmi, BmiModelType>,
                  "BmiModelType must derive from bmi::Bmi");
    static_assert(std::is_default_constructible_v<BmiModelType>,
                  "BmiModelType must be default constructible");

    /**
     * @brief Execute one BMI-wrapped simulation.
     * @param task Task arguments where args[0] is the BMI config path.
     * @return Success or error message for this execution.
     */
    std::string execute(const Task& task) override {
        if (task.args.empty()) {
            return "BMI error: missing config path in task specification";
        }
        const auto* config_file = caf::get_if<std::string>(&task.args.front());
        if (!config_file) {
            return "BMI error: first task argument must be a config path string";
        }

        BmiModelType bmi_model;

        bool model_initialized = false;
        std::size_t update_steps = 0;

        try {
            // Initialize a fresh BMI model instance for this BMI-wrapped simulation
            bmi_model.Initialize(*config_file);
            model_initialized = true;

            // Advance the model with Update until it reaches its declared end time
            const double end_time = bmi_model.GetEndTime();
            while (bmi_model.GetCurrentTime() < end_time) {
                bmi_model.Update();
                ++update_steps;
            }

            // Finalize to release BMI model resources before returning
            bmi_model.Finalize();
            model_initialized = false;
        } catch (const std::exception& exception) {
            // Catch any exceptions thrown by the BMI model, and finalize if needed
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            return std::string("BMI error: ") + exception.what();
        } catch (...) {
            // Catch any non-standard exceptions and finalize if needed
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            return "BMI error: unknown exception";
        }

        std::ostringstream message;
        message << "BMI workflow complete (updates: " << update_steps << ")";
        return message.str();
    }
};

} // namespace miaow
