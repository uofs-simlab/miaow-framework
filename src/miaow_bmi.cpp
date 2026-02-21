#include <exception>
#include <sstream>
#include <string>
#include <utility>

#include "miaow_bmi.hpp"

namespace miaow {

BmiSimulationUnit::BmiSimulationUnit(BmiModelFactory bmi_model_factory)
    : bmi_model_factory_(std::move(bmi_model_factory)) {
}

std::string BmiSimulationUnit::execute(const std::string& config_file) {
    // Check that the factory is set and can create a BMI model instance before proceeding.
    if (!bmi_model_factory_) {
        return "BMI error: model factory is not set";
    }
    // Create a new BMI model instance for this execution using the factory.
    std::unique_ptr<bmi::Bmi> bmi_model = bmi_model_factory_();
    
    // Check that the factory returned a valid BMI model instance before proceeding.
    if (!bmi_model) {
        return "BMI error: model factory returned null";
    }

    bool model_initialized = false;
    std::size_t update_steps = 0;

    try {
        // Create and initialize a new BMI model instance for this execution.
        bmi_model->Initialize(config_file);
        model_initialized = true;

        // Advance the model with Update until it reaches its declared end time.
        const double end_time = bmi_model->GetEndTime();
        while (bmi_model->GetCurrentTime() < end_time) {
            bmi_model->Update();
            ++update_steps;
        }

        // Finalize to release BMI model resources before returning.
        bmi_model->Finalize();
        model_initialized = false;
    } catch (const std::exception& exception) {
        // Catch any exceptions thrown by the BMI model, and finalize if needed.
        if (model_initialized) {
            try {
                bmi_model->Finalize();
            } catch (...) {
            }
        }
        return std::string("BMI error: ") + exception.what();
    } catch (...) {
        // Catch any non-standard exceptions and finalize if needed.
        if (model_initialized) {
            try {
                bmi_model->Finalize();
            } catch (...) {
            }
        }
        return "BMI error: unknown exception";
    }

    std::ostringstream message;
    message << "BMI workflow complete (updates: " << update_steps << ")";
    return message.str();
}

} // namespace miaow
