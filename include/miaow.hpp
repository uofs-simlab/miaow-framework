#pragma once

#include <cstddef>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace miaow {

/**
 * @brief Base class for model-specific simulation units.
 *
 * Runtime dispatch occurs through subclasses overriding execute().
 */
class SimulationUnit {
public:
    /// @brief Virtual destructor for polymorphic cleanup.
    virtual ~SimulationUnit() = default;

    /**
     * @brief Execute the simulation unit for a single task.
     * @param config_file Path to task configuration input.
     * @return Result message for this execution.
     */
    virtual std::string execute(const std::string& config_file) = 0;
};

/**
 * @brief Callable behavior used by each simulation_unit_actor.
 *
 * The executor receives a task config file and returns a result string.
 */
using SimulationUnitExecutor = std::function<std::string(const std::string& config_file)>;

/**
 * @brief Runtime options for model-independent orchestration behavior.
 *
 * Solver-specific options should be passed in the config file and not included here.
 */
struct RuntimeOptions {
    /// Timeout for any single request from orchestrator to a worker actor.
    std::chrono::milliseconds request_timeout_;

    /// Maximum number of worker actors that may run concurrently.
    std::size_t max_workers_;

    /**
     * @brief Construct runtime options with safe defaults.
     * @param timeout Request timeout used between actors.
     * @param workers Desired worker count. Values of zero are clamped to one.
     */
    RuntimeOptions(std::chrono::milliseconds timeout = std::chrono::seconds(1),
                   std::size_t workers = 1)
        : request_timeout_(timeout),
          max_workers_(workers == 0 ? 1 : workers) {
    }
};

/**
 * @brief Actor runtime for orchestrating simulation tasks.
 *
 * The runtime is model-agnostic and thread-safe.
 */
class Runtime {
public:
    /**
     * @brief Construct runtime from an executor and options.
     * @param simulation_unit_executor Callable used by simulation_unit_actor.
     * @param options Runtime scheduling and timeout options.
     */
    Runtime(SimulationUnitExecutor simulation_unit_executor, RuntimeOptions options = {});

    /**
     * @brief Run a single-task workflow.
     * @param config_file Task configuration path.
     * @return Result message from the task execution.
     */
    std::string run_workflow(const std::string& config_file) const;

    /**
     * @brief Run a multi-task workflow.
     * @param config_files Task configuration paths.
     * @return Per-task result messages in input order.
     */
    std::vector<std::string> run_workflow(const std::vector<std::string>& config_files) const;

private:
    SimulationUnitExecutor simulation_unit_executor_;
    RuntimeOptions runtime_options_;
};

/**
 * @brief Create a runtime from a concrete SimulationUnit type.
 *
 * Builds a SimulationUnitExecutor that constructs a fresh unit for each task execution.
 *
 * @tparam SimulationUnitType Concrete simulation unit type.
 * @tparam ConstructorArgs Constructor argument types for SimulationUnitType.
 * @param options Runtime scheduling and timeout options.
 * @param constructor_args Arguments forwarded to SimulationUnitType constructor.
 * @return Runtime configured to execute SimulationUnitType tasks.
 */
template <class SimulationUnitType, class... ConstructorArgs>
Runtime make_runtime(RuntimeOptions options, ConstructorArgs... constructor_args) {
    static_assert(std::is_base_of_v<SimulationUnit, SimulationUnitType>,
                  "SimulationUnitType must derive from miaow::SimulationUnit");

    auto stored_constructor_args =
        std::make_tuple(std::move(constructor_args)...);

    SimulationUnitExecutor simulation_unit_executor =
        [stored_constructor_args = std::move(stored_constructor_args)](
            const std::string& config_file) -> std::string {
            std::unique_ptr<SimulationUnit> simulation_unit = std::apply(
                [](const auto&... args) -> std::unique_ptr<SimulationUnit> {
                    return std::make_unique<SimulationUnitType>(args...);
                },
                stored_constructor_args);
            if (!simulation_unit) {
                return "SimulationUnit error: failed to create simulation unit";
            }
            return simulation_unit->execute(config_file);
        };

    return Runtime(std::move(simulation_unit_executor), std::move(options));
}

} // namespace miaow
