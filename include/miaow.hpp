#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace miaow {

/**
 * Base class for model-specific simulation units.
 * Runtime dispatch occurs through subclasses overriding execute().
 */
class SimulationUnit {
public:
    virtual ~SimulationUnit() = default;
    virtual std::string execute(const std::string& config_file) = 0;
};

/**
 * Runtime options for model-independent orchestration behaviour. Solver-specific options
 * should be passed in the config file and not included here.
 */
struct RuntimeOptions {
    std::chrono::milliseconds request_timeout{std::chrono::seconds(1)};
};

/**
 * MIAOW runtime that manages actors for orchestrating model runs. The runtime is designed 
 * to be model-agnostic and can be used with any model that can be wrapped in a simulation 
 * unit actor. The runtime is also designed to be thread-safe and can be used in 
 * multi-threaded applications.
 */
class Runtime {
public:
    Runtime(std::shared_ptr<SimulationUnit> simulation_unit, RuntimeOptions options = {});

    /**
     * Run orchestrator -> job -> simulation unit with a model config path.
     * The config path is intended to be passed (for example) to the simulation unit's 
     * initialization function, which may be BMI Initialize or similar.
     */
    std::string run_workflow(const std::string& config_file) const;

private:
    std::shared_ptr<SimulationUnit> simulation_unit_;
    RuntimeOptions runtime_options_;
};

} // namespace miaow
