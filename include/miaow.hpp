#pragma once

#include <cstddef>
#include <chrono>
#include <fstream>
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
    /// @brief Virtual destructor for polymorphic cleanup
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
    /// Maximum number of worker actors that may run concurrently
    std::size_t max_workers_;

    /// Timeout for a single task request from job_actor to simulation_unit_actor
    std::chrono::milliseconds task_request_timeout_;

    /// Timeout for whole-job requests between runtime/orchestrator/job actors
    std::chrono::milliseconds job_request_timeout_;

    /**
     * @brief Construct runtime options with safe defaults.
     * @param workers Desired worker count. Values of zero are clamped to one.
     * @param task_timeout Timeout used for one simulation task request.
     * @param job_timeout Timeout used for whole-job requests.
     */
    RuntimeOptions(std::size_t workers = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::seconds(5),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5))
        : max_workers_(workers == 0 ? 1 : workers),
          task_request_timeout_(task_timeout),
          job_request_timeout_(job_timeout) {
    }
};

/**
 * @brief Workflow timing diagnostics
 */
struct JobDiagnostics {
    /// Total wall-clock runtime for the full job in seconds
    double total_time_s{0.0};

    /// Wall-clock duration for each task by task ID in seconds
    std::vector<double> task_times_s;
};

/**
 * @brief Result payload returned by Runtime::run_workflow
 */
struct WorkflowResult {
    /// Result message for each task in input order
    std::vector<std::string> task_results;

    /// Timing diagnostics for this workflow run
    JobDiagnostics diagnostics;
};

template <class Inspector>
bool inspect(Inspector& inspector, JobDiagnostics& diagnostics) {
    // Defines how CAF serializes/deserializes JobDiagnostics for actor messaging
    return inspector.apply(diagnostics.total_time_s) &&
           inspector.apply(diagnostics.task_times_s);
}

template <class Inspector>
bool inspect(Inspector& inspector, WorkflowResult& workflow_result) {
    // Defines how CAF serializes/deserializes WorkflowResult for actor messaging
    return inspector.apply(workflow_result.task_results) &&
           inspector.apply(workflow_result.diagnostics);
}

/**
 * @brief Format diagnostics as a human-readable block
 */
std::string format_diagnostics(const JobDiagnostics& diagnostics, std::size_t job_id = 0);

/**
 * @brief Print diagnostics to stdout
 */
void print_diagnostics(const JobDiagnostics& diagnostics, std::size_t job_id = 0);

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
     * @return Workflow result with task messages and diagnostics.
     */
    WorkflowResult run_workflow(const std::string& config_file) const;

    /**
     * @brief Run a multi-task workflow.
     * @param config_files Task configuration paths.
     * @return Workflow result with task messages and diagnostics.
     */
    WorkflowResult run_workflow(const std::vector<std::string>& config_files) const;

private:
    SimulationUnitExecutor simulation_unit_executor_;
    RuntimeOptions runtime_options_;
};

/**
 * @brief Validate config file inputs before dispatching tasks
 *
 * Returns an empty string on success, or an error message on failure
 */
inline std::string validate_config_paths(const std::vector<std::string>& config_files) {
    if (config_files.empty()) {
        return "Error: at least one config file must be provided";
    }
    for (const std::string& config_file : config_files) {
        std::ifstream input(config_file);
        if (!input.good()) {
            return std::string("Error: could not open config file: ") + config_file;
        }
    }
    return "";
}

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
