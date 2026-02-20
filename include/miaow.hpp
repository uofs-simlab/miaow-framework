#pragma once

#include <chrono>
#include <string>

namespace miaow {

/**
 * Runtime options for model-independent orchestration behaviour. Solver-specific options
 *  should be passed in the config file and not included here.
 */
struct runtime_options {
    std::chrono::milliseconds request_timeout{std::chrono::seconds(1)};
};

/**
 * MIAOW runtime that manages actors for orchestrating model runs. The runtime is designed 
 * to be model-agnostic and can be used with any model that can be wrapped in a simulation 
 * unit actor. The runtime is also designed to be thread-safe and can be used in 
 * multi-threaded applications.
 */
class runtime {
public:
    explicit runtime(runtime_options options = {});

    /**
     * Run orchestrator -> job -> simulation unit with a model config path.
     * The config path is intended to be passed (for example) to the simulation unit's 
     * initialization function, which may be BMI Initialize or similar.
     */
    std::string run(const std::string& config_file) const;

private:
    runtime_options runtime_options_;
};

/**
 * Convenience API that constructs a default runtime and runs once, passing the config file 
 * path to the orchestrator. The config file is intended to be passed (for example) to the 
 * simulation unit's initialization function, which may be BMI Initialize or similar.
 */
std::string miaow_run(const std::string& config_file);

} // namespace miaow
