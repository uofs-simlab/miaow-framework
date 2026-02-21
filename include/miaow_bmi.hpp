#pragma once

#include <functional>
#include <memory>
#include <string>

#include "miaow.hpp"
#include "third_party/bmi.hxx"

namespace miaow {

/**
 * Factory type for creating new BMI model instances, which is a function that returns a 
 * unique pointer to a new BMI model instance. This factory is used by the 
 * BmiSimulationUnit to create a fresh BMI model instance for each execution.
 */
using BmiModelFactory = std::function<std::unique_ptr<bmi::Bmi>()>;

/**
 * Simulation unit that runs a complete BMI model lifecycle for one workflow call.
 */
class BmiSimulationUnit : public SimulationUnit {
public:
    /**
     * Construct a BMI simulation unit with a factory that creates a new BMI model instance 
     * for each workflow execution. The factory is used to ensure that each execution gets 
     * a fresh BMI model instance.
     */
    explicit BmiSimulationUnit(BmiModelFactory bmi_model_factory);

    /**
     * Execute a BMI workflow by creating a new BMI-wrapped model instance from the factory,
     * initializing it with the config file, advancing it with Update until it reaches its
     * declared end time, and finalizing it to release resources. Any exceptions thrown by
     * the BMI model are caught and returned as error messages. The return value is a
     * string indicating success or the error.
     */
    std::string execute(const std::string& config_file) override;

private:
    // Factory instance for creating new BMI model instances for each execution.
    BmiModelFactory bmi_model_factory_;
};

} // namespace miaow
