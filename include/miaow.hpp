#pragma once

#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <caf/actor.hpp>
#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/config_value.hpp>

namespace miaow {

/**
 * @brief Generic task input specification.
 *
 * Arguments are positional and interpreted by the concrete SimulationUnit.
 */
struct Task {
    /// Generic arguments for this task to pass into `SimulationUnit::execute(args)`
    std::vector<caf::config_value> args;

    Task() = default;

    /**
     * @brief Construct a Task from a vector of config values.
     * @param task_args Positional arguments for this task.
     */
    explicit Task(std::vector<caf::config_value> task_args)
        : args(std::move(task_args)) {
    }

    /**
     * @brief Construct a Task from a single config value.
     * @param arg Single config value for this task.
     */
    explicit Task(caf::config_value arg)
        : args{std::move(arg)} {
    }

    /**
     * @brief Construct a Task from a single string argument.
     * @param arg Single string argument for this task.
     */
    explicit Task(std::string arg)
        : args{caf::config_value{std::move(arg)}} {
    }
};

/**
 * @brief Base class for model-specific simulation units.
 *
 * Runtime dispatch occurs through subclasses overriding execute().
 */
class SimulationUnit {
public:
    virtual ~SimulationUnit() = default;

    /**
     * @brief Execute the simulation unit for a single task.
     * @param task Generic task arguments.
     * @return Result message for this execution.
     */
    virtual std::string execute(const Task& task) = 0;
};

/**
 * @brief Callable behavior used by each simulation_unit_actor.
 *
 * The executor receives generic task arguments and returns a result string.
 */
using SimulationUnitExecutor = std::function<std::string(const Task& task)>;

/**
 * @brief Runtime options for model-independent orchestration behavior.
 *
 * Solver-specific options should be passed in task arguments and not included here.
 */
struct RuntimeOptions {
    /// Maximum number of worker actors that may run concurrently
    std::size_t max_workers_;

    /// Timeout for a single task request from `job_actor` to `simulation_unit_actor`
    std::chrono::milliseconds task_request_timeout_;

    /// Timeout for whole-job requests between `runtime`/`orchestrator`/`job_actor`
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
 * @brief Job timing diagnostics
 */
struct JobDiagnostics {
    /// Total wall-clock runtime for the full job in seconds
    double total_time_s{0.0};

    /// Wall-clock duration for each task by task ID in seconds
    std::vector<double> task_times_s;
};

/**
 * @brief Result payload returned when awaiting a Runtime::submit_job handle
 */
struct JobResult {
    /// Result message for each task in input order
    std::vector<std::string> task_results;

    /// Timing diagnostics for this job run
    JobDiagnostics diagnostics;
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result.
 */
using JobHandle = std::future<JobResult>;

/// Monotonic identifier assigned to each submitted job.
using JobId = std::uint64_t;

/**
 * @brief Runtime-owned registry of in-progress jobs.
 *
 * This is effectively a dictionary/hash map:
 * `job_id -> promise_for_that_job_result`.
 *
 * Each submitted `JobId` maps to the promise that will be fulfilled by the
 * orchestrator actor when that job finishes.
 */
using PendingJobs = std::unordered_map<JobId, std::promise<JobResult>>;

template <class Inspector>
bool inspect(Inspector& inspector, Task& task) {
    // Defines how CAF serializes/deserializes Task for actor messaging
    return inspector.apply(task.args);
}

template <class Inspector>
bool inspect(Inspector& inspector, JobDiagnostics& diagnostics) {
    // Defines how CAF serializes/deserializes JobDiagnostics for actor messaging
    return inspector.apply(diagnostics.total_time_s) &&
           inspector.apply(diagnostics.task_times_s);
}

template <class Inspector>
bool inspect(Inspector& inspector, JobResult& job_result) {
    // Defines how CAF serializes/deserializes JobResult for actor messaging
    return inspector.apply(job_result.task_results) &&
           inspector.apply(job_result.diagnostics);
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
     * @brief Submit one task as an asynchronous job.
     * @param task Generic task arguments.
     * @return Handle for collecting task messages and diagnostics.
     */
    JobHandle submit_job(const Task& task) const;

    /**
     * @brief Submit a multi-task asynchronous job (copy path).
     *
     * This `const&` overload accepts caller-owned vectors (lvalues) and copies
     * tasks so the caller keeps ownership unchanged.
     * @param tasks Generic task arguments.
     * @return Handle for collecting task messages and diagnostics.
     */
    JobHandle submit_job(const std::vector<Task>& tasks) const;

    /**
     * @brief Submit a multi-task asynchronous job (move path).
     *
     * This `&&` overload accepts rvalues (temporaries or `std::move(...)`) and
     * transfers task ownership into Runtime to avoid an extra copy.
     * @param tasks Generic task arguments.
     * @return Handle for collecting task messages and diagnostics.
     */
    JobHandle submit_job(std::vector<Task>&& tasks) const;

private:
    /// Shared simulation unit executor used by all simulation_unit actors in this runtime
    SimulationUnitExecutor simulation_unit_executor_;
    
    /// Runtime options for this runtime, copied into each job_actor's JobState
    RuntimeOptions runtime_options_;

    /// Monotonic counter for generating unique JobIds for each submitted job
    mutable std::atomic<JobId> next_job_id_{1};

    /// Mutex to protect access to the pending_jobs dictionary
    mutable std::mutex pending_jobs_mutex_;

    /// Dictionary mapping in-progress JobIds to their completion promises
    mutable PendingJobs pending_jobs_;

    /// CAF actor system configuration for this runtime instance
    std::shared_ptr<caf::actor_system_config> actor_system_config_;

    /// CAF actor system for this runtime instance
    std::shared_ptr<caf::actor_system> actor_system_;

    /// Actor that receives requests from Runtime, spawns one job_actor per request,
    /// and fulfills Runtime promises when jobs complete
    caf::actor orchestrator_;
};

/**
 * @brief Validate config path inputs for config-based helpers
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
    return ""; // return empty string on success
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
    // Check that SimulationUnitType is a valid simulation unit class at compile time
    static_assert(std::is_base_of_v<SimulationUnit, SimulationUnitType>,
                  "SimulationUnitType must derive from miaow::SimulationUnit");

    // Store constructor arguments in a tuple
    auto stored_constructor_args = std::make_tuple(std::move(constructor_args)...);

    // Build a SimulationUnitExecutor (an anonymous function) that constructs a fresh 
    // instance of SimulationUnitType for each task execution
    SimulationUnitExecutor simulation_unit_executor =
        [stored_constructor_args = std::move(stored_constructor_args)](
            const Task& task) -> std::string {
            // Create a new SimulationUnitType instance for this task execution
            std::unique_ptr<SimulationUnit> simulation_unit = std::apply(
                [](const auto&... args) -> std::unique_ptr<SimulationUnit> {
                    return std::make_unique<SimulationUnitType>(args...);
                }, stored_constructor_args);
        
            // If we failed to create a valid simulation unit, return an error message
            if (!simulation_unit) {
                return "SimulationUnit error: failed to create simulation unit";
            }
            
            // Execute the simulation unit with the given task and return the result
            return simulation_unit->execute(task);
        };

    // Return a Runtime constructed with this SimulationUnitExecutor and the given options
    return Runtime(std::move(simulation_unit_executor), std::move(options));
}

} // namespace miaow
