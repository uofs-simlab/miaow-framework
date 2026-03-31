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

/// Monotonic identifier assigned to each registered simulation unit
using SimulationUnitId = std::uint64_t;
/// Monotonic identifier assigned to each task within one submitted job
using TaskId = std::uint64_t;
/// Monotonic identifier assigned to each submitted job
using JobId = std::uint64_t;

/**
 * @brief Generic task input specification.
 *
 * Arguments are positional and interpreted by the concrete SimulationUnit.
 */
struct Task {
    /// Simulation unit ID that selects which registered executor handles this task
    SimulationUnitId simulation_unit_id_{0};

    /// Generic arguments for this task to pass into `SimulationUnit::execute(args)`
    std::vector<caf::config_value> args_;

    /// Runtime-injected directory where this task should write output artifacts
    std::string task_output_dir_;

    Task() = default;

    /**
     * @brief Construct a Task for a specific simulation unit from config values.
     * @param task_simulation_unit_id Simulation unit ID for this task.
     * @param task_args Positional arguments for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::vector<caf::config_value> task_args)
        : simulation_unit_id_(task_simulation_unit_id),
          args_(std::move(task_args)) {
    }

    /**
     * @brief Construct a Task for a specific simulation unit from one config value.
     * @param task_simulation_unit_id Simulation unit ID for this task.
     * @param arg Single config value for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, caf::config_value arg)
        : simulation_unit_id_(task_simulation_unit_id),
          args_{std::move(arg)} {
    }

    /**
     * @brief Construct a Task for a specific simulation unit from one string argument.
     * @param task_simulation_unit_id Simulation unit ID for this task.
     * @param arg Single string argument for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::string arg)
        : simulation_unit_id_(task_simulation_unit_id),
          args_{caf::config_value{std::move(arg)}} {
    }
};

/**
 * @brief Per-task completion status.
 */
enum class TaskStatus : std::uint8_t {
    succeeded = 0,
    failed = 1
};

/**
 * @brief Structured result payload returned for one task execution.
 */
struct TaskResult {
    /// Task ID within the submitted job
    TaskId task_id_{0};

    /// Simulation unit ID that executed this task
    SimulationUnitId simulation_unit_id_{0};

    /// Success/failure state for this task
    TaskStatus status_{TaskStatus::failed};

    /// Message interpreted by `status_` as output or failure detail
    std::string message_;

    /// Relative paths to task artifacts produced under task_output_dir_
    std::vector<std::string> artifact_paths_;

    /// Wall-clock runtime for this task in seconds
    double run_time_s_{0.0};
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
     * @return Structured result for this execution.
     */
    virtual TaskResult execute(const Task& task) = 0;
};

/**
 * @brief Callable behaviour used by each simulation_unit_actor.
 *
 * The executor receives generic task arguments and returns a structured result.
 */
using SimulationUnitExecutor = std::function<TaskResult(const Task& task)>;

/**
 * @brief Registry for simulation unit executors.
 *
 * Tracks `simulation_unit_id -> executor` for runtime task routing.
 */
class SimulationUnitRegistry {
public:
    SimulationUnitRegistry() = default;

    /**
     * @brief Register a concrete SimulationUnit type and return its assigned ID.
     *
     * Builds an executor that constructs a fresh SimulationUnitType for each task.
     *
     * @tparam SimulationUnitType Concrete simulation unit type.
     * @tparam ConstructorArgs Constructor argument types for SimulationUnitType.
     * @param constructor_args Arguments forwarded to SimulationUnitType constructor.
     * @return SimulationUnitId assigned to this registered unit.
     */
    template <class SimulationUnitType, class... ConstructorArgs>
    SimulationUnitId add(ConstructorArgs... constructor_args) {
        // Check that SimulationUnitType is a valid simulation unit class at compile time
        static_assert(std::is_base_of_v<SimulationUnit, SimulationUnitType>,
                      "SimulationUnitType must derive from miaow::SimulationUnit");

        // Store constructor arguments in a tuple
        std::tuple<std::decay_t<ConstructorArgs>...> stored_constructor_args(
            std::move(constructor_args)...);

        // Build a SimulationUnitExecutor (an anonymous function) that constructs a fresh
        // instance of SimulationUnitType for each task execution
        SimulationUnitExecutor simulation_unit_executor =
            [stored_constructor_args = std::move(stored_constructor_args)](
                const Task& task) -> TaskResult {
                // Create a new SimulationUnitType instance for this task execution
                std::unique_ptr<SimulationUnit> simulation_unit = std::apply(
                    [](const std::decay_t<ConstructorArgs>&... args)
                        -> std::unique_ptr<SimulationUnit> {
                        return std::make_unique<SimulationUnitType>(args...);
                    }, stored_constructor_args);

                // If we failed to create a valid simulation unit, return an error message
                if (!simulation_unit) {
                    TaskResult task_result;
                    task_result.simulation_unit_id_ = task.simulation_unit_id_;
                    task_result.status_ = TaskStatus::failed;
                    task_result.message_ = "SimulationUnit error: failed to create simulation unit";
                    return task_result;
                }

                // Execute the simulation unit with the given task and return the result
                return simulation_unit->execute(task);
            };

        const SimulationUnitId simulation_unit_id = next_simulation_unit_id_++;
        simulation_unit_executors_by_id_.emplace(simulation_unit_id,
                                                 std::move(simulation_unit_executor));
        return simulation_unit_id;
    }

    /**
     * @brief Return true when no simulation units have been registered.
     */
    bool empty() const {
        return simulation_unit_executors_by_id_.empty();
    }

    /**
     * @brief Look up executor for a simulation unit ID.
     * @param simulation_unit_id Registered ID to resolve.
     * @return Pointer to executor, or null when ID is unknown.
     */
    const SimulationUnitExecutor* find_executor(SimulationUnitId simulation_unit_id) const {
        const std::unordered_map<SimulationUnitId, SimulationUnitExecutor>::const_iterator
            simulation_unit_entry = simulation_unit_executors_by_id_.find(simulation_unit_id);
        if (simulation_unit_entry == simulation_unit_executors_by_id_.end()) {
            return nullptr;
        }
        return &simulation_unit_entry->second;
    }

private:
    std::unordered_map<SimulationUnitId, SimulationUnitExecutor>
        simulation_unit_executors_by_id_;
    SimulationUnitId next_simulation_unit_id_{1};
};

/**
 * @brief Runtime options for model-independent orchestration behaviour.
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

    /// Root output directory where run/job/task artifact folders are created
    std::string output_root_;

    /**
     * @brief Construct runtime options with safe defaults.
     * @param workers Desired worker count. Values of zero are clamped to one.
     * @param task_timeout Timeout used for one simulation task request.
     * @param job_timeout Timeout used for whole-job requests.
     * @param output_root Root directory for runtime-managed task output folders.
     */
    RuntimeOptions(std::size_t workers = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::seconds(5),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5),
                   std::string output_root = "./miaow_output")
        : max_workers_(workers == 0 ? 1 : workers),
          task_request_timeout_(task_timeout),
          job_request_timeout_(job_timeout),
          output_root_(std::move(output_root)) {
    }
};

/**
 * @brief Job timing diagnostics
 */
struct JobDiagnostics {
    /// Total wall-clock runtime for the full job in seconds
    double total_time_s_{0.0};

    /// Wall-clock duration for each task by task ID in seconds
    std::vector<double> task_times_s_;
};

/**
 * @brief Result payload returned when awaiting a Runtime::submit_job handle
 */
struct JobResult {
    /// Structured result for each task in input order
    std::vector<TaskResult> task_results_;

    /// Timing diagnostics for this job run
    JobDiagnostics diagnostics_;
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result.
 */
using JobHandle = std::future<JobResult>;

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
    return inspector.apply(task.simulation_unit_id_) &&
           inspector.apply(task.args_) &&
           inspector.apply(task.task_output_dir_);
}

template <class Inspector>
bool inspect(Inspector& inspector, TaskStatus& task_status) {
    // Defines how CAF serializes/deserializes TaskStatus for actor messaging
    std::uint8_t status_value = static_cast<std::uint8_t>(task_status);
    const bool inspected = inspector.apply(status_value);
    if (!inspected) {
        return false;
    }
    task_status = static_cast<TaskStatus>(status_value);
    return true;
}

template <class Inspector>
bool inspect(Inspector& inspector, TaskResult& task_result) {
    // Defines how CAF serializes/deserializes TaskResult for actor messaging
    return inspector.apply(task_result.task_id_) &&
           inspector.apply(task_result.simulation_unit_id_) &&
           inspector.apply(task_result.status_) &&
           inspector.apply(task_result.message_) &&
           inspector.apply(task_result.artifact_paths_) &&
           inspector.apply(task_result.run_time_s_);
}

template <class Inspector>
bool inspect(Inspector& inspector, JobDiagnostics& diagnostics) {
    // Defines how CAF serializes/deserializes JobDiagnostics for actor messaging
    return inspector.apply(diagnostics.total_time_s_) &&
           inspector.apply(diagnostics.task_times_s_);
}

template <class Inspector>
bool inspect(Inspector& inspector, JobResult& job_result) {
    // Defines how CAF serializes/deserializes JobResult for actor messaging
    return inspector.apply(job_result.task_results_) &&
           inspector.apply(job_result.diagnostics_);
}

/**
 * @brief Format diagnostics as a human-readable block
 */
std::string format_diagnostics(const JobDiagnostics& diagnostics, JobId job_id = 0);

/**
 * @brief Print diagnostics to stdout
 */
void print_diagnostics(const JobDiagnostics& diagnostics, JobId job_id = 0);

/**
 * @brief Actor runtime for orchestrating simulation tasks.
 *
 * The runtime is model-agnostic and thread-safe.
 */
class Runtime {
public:
    /**
     * @brief Construct runtime from a simulation unit registry with default options.
     * @param simulation_unit_registry Registry mapping simulation_unit_id to executor.
     */
    Runtime(const SimulationUnitRegistry& simulation_unit_registry);

    /**
     * @brief Construct runtime from a simulation unit registry and options.
     * @param simulation_unit_registry Registry mapping simulation_unit_id to executor.
     * @param options Runtime scheduling and timeout options.
     */
    Runtime(const SimulationUnitRegistry& simulation_unit_registry,
            RuntimeOptions options);

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
    /// Shared simulation unit registry used by all simulation_unit actors in this runtime
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry_;
    
    /// Runtime options for this runtime, copied into each job_actor's JobState
    RuntimeOptions runtime_options_;

    /// Monotonic run ID used to create deterministic output directory layouts
    std::uint64_t run_id_{0};

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

} // namespace miaow
