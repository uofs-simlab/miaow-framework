#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <initializer_list>
#include <map>
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

namespace core {
struct SubprocessState;
}

/**
 * @brief Monotonic identifier assigned to each registered `SimulationUnit`.
 */
using SimulationUnitId = std::uint64_t;

/**
 * @brief Monotonic identifier assigned to each task within one submitted job.
 */
using TaskId = std::uint64_t;

/**
 * @brief Monotonic identifier assigned to each submitted job.
 */
using JobId = std::uint64_t;

/**
 * @brief Descriptive task metadata preserved for reporting and postprocessing.
 */
using TaskMetadata = std::map<std::string, std::string>;

/**
 * @brief Execution location and process lifetime selected per `Task`.
 */
enum class ExecutionMode {
    // Execute the task in a parent-process worker actor
    in_process,

    // Launch one subprocess for one task and stop it after the result
    single_task_subprocess,

    // Reuse one subprocess owned by a job worker for subprocess-routed tasks
    persistent_subprocess,
};

/**
 * @brief Generic task input specification.
 *
 * Arguments are positional and interpreted by the concrete `SimulationUnit`.
 */
struct Task {
    // `SimulationUnitId` that selects which registered unit handles this task
    SimulationUnitId simulation_unit_id_{0};

    // Execution mode selected for this task
    ExecutionMode execution_mode_{ExecutionMode::in_process};

    // Optional subprocess executable path set on this task
    // Empty uses the MIAOW_SUBPROCESS_PATH compile-time default
    std::string subprocess_path_;

    // Generic arguments for this task to pass into `SimulationUnit::execute()`
    std::vector<caf::config_value> args_;

    // Directory injected by `Runtime` where this task should write artifacts
    std::string task_output_dir_;

    // Stable task label injected by `Runtime` and used as an artifact file stem
    std::string task_name_;

    // Metadata preserves labels used by reporting and postprocessing tools
    TaskMetadata metadata_;

    Task() = default;

    /**
     * @brief Construct a `Task` for a specific simulation unit from config values.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param task_args Positional arguments for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::vector<caf::config_value> task_args)
        : simulation_unit_id_(task_simulation_unit_id), args_(std::move(task_args)) {}

    /**
     * @brief Construct a `Task` from config values and descriptive metadata.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param task_args Positional arguments for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::vector<caf::config_value> task_args,
         TaskMetadata task_metadata)
        : simulation_unit_id_(task_simulation_unit_id), args_(std::move(task_args)),
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` for a specific simulation unit from one config value.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single `caf::config_value` for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, caf::config_value arg)
        : simulation_unit_id_(task_simulation_unit_id), args_{std::move(arg)} {}

    /**
     * @brief Construct a `Task` from one config value and descriptive metadata.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single `caf::config_value` for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(SimulationUnitId task_simulation_unit_id, caf::config_value arg,
         TaskMetadata task_metadata)
        : simulation_unit_id_(task_simulation_unit_id), args_{std::move(arg)},
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` for a specific simulation unit from one string.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single string argument for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::string arg)
        : simulation_unit_id_(task_simulation_unit_id),
          args_{caf::config_value{std::move(arg)}} {}

    /**
     * @brief Construct a `Task` for a specific simulation unit from string arguments.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param string_args Positional string arguments for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id,
         std::initializer_list<std::string> string_args)
        : simulation_unit_id_(task_simulation_unit_id) {
        args_.reserve(string_args.size());
        for (const std::string& string_arg : string_args) {
            args_.emplace_back(string_arg);
        }
    }

    /**
     * @brief Return a pointer to a string at a given positional argument index.
     * @param arg_index Positional argument index.
     * @return Pointer to string value, or null if missing or not a string.
     */
    const std::string* string_arg(std::size_t arg_index) const {
        if (arg_index >= args_.size()) {
            return nullptr;
        }
        return caf::get_if<std::string>(&args_[arg_index]);
    }
};

/**
 * @brief Per-task completion status.
 */
enum class TaskStatus : std::uint8_t { succeeded = 0, failed = 1 };

/**
 * @brief Convert a `TaskStatus` to a stable string value.
 * @param status `TaskStatus` enum value.
 * @return Stable string representation of `status`.
 */
inline std::string task_status_to_string(TaskStatus status) {
    if (status == TaskStatus::succeeded) {
        return "succeeded";
    }
    return "failed";
}

/**
 * @brief Simulation-unit result payload returned for one `Task` execution.
 */
struct TaskResult {
    TaskResult() = default;

    /**
     * @brief Construct a task result from status, message, and artifact paths.
     * @param status Terminal status for this task execution.
     * @param message Message interpreted by `status` as output or failure detail.
     * @param artifact_paths Relative paths to artifacts under the job directory.
     */
    TaskResult(TaskStatus status, std::string message,
               std::vector<std::string> artifact_paths = {})
        : status_{status}, message_{std::move(message)},
          artifact_paths_{std::move(artifact_paths)} {}

    // Simulation-unit status for this task execution
    TaskStatus status_{TaskStatus::failed};

    // Message interpreted by `status_` as output or failure detail
    std::string message_;

    // Relative paths to task artifacts produced under the job directory
    std::vector<std::string> artifact_paths_;
};

/**
 * @brief Runtime-owned record for one completed task.
 */
struct TaskRecord {
    TaskRecord() = default;

    /**
     * @brief Construct a completed task record from task input and terminal result.
     * @param task Submitted task whose metadata and simulation unit ID are copied.
     * @param task_id `TaskId` within the submitted job.
     * @param result Terminal simulation unit result for this task.
     * @param run_time_s Wall-clock runtime for this task in seconds.
     */
    TaskRecord(const Task& task, TaskId task_id, TaskResult result, double run_time_s)
        : task_id_{task_id}, simulation_unit_id_{task.simulation_unit_id_},
          task_name_{task.task_name_}, metadata_{task.metadata_},
          result_{std::move(result)}, run_time_s_{run_time_s} {}

    // `TaskId` within the submitted job
    TaskId task_id_{0};

    // `SimulationUnitId` that executed this task
    SimulationUnitId simulation_unit_id_{0};

    // Stable task label assigned by `Runtime`
    std::string task_name_;

    // Metadata copied from the submitted task
    TaskMetadata metadata_;

    // Simulation-unit result for this task
    TaskResult result_;

    // Wall-clock runtime for this task in seconds
    double run_time_s_{0.0};
};

/**
 * @brief Runtime-owned record returned for one submitted job.
 */
struct JobRecord {
    JobRecord() = default;

    /**
     * @brief Construct a final job record from job IDs, task records, and runtime.
     * @param run_id Runtime identifier for this `Runtime` instance.
     * @param job_id `JobId` assigned to this submitted job.
     * @param tasks Task records in submitted task order.
     * @param run_time_s Wall-clock runtime for this job in seconds.
     */
    JobRecord(std::uint64_t run_id, JobId job_id, std::vector<TaskRecord> tasks = {},
              double run_time_s = 0.0)
        : run_id_{run_id}, job_id_{job_id}, tasks_{std::move(tasks)},
          run_time_s_{run_time_s} {}

    // Runtime identifier for this `Runtime` instance
    std::uint64_t run_id_{0};

    // `JobId` assigned to this submitted job
    JobId job_id_{0};

    // Task records in submitted task order
    std::vector<TaskRecord> tasks_;

    // Wall-clock runtime for this job in seconds
    double run_time_s_{0.0};
};

/**
 * @brief Build a failed `TaskResult` from a message.
 * @param message Failure message for the result.
 * @param prefix Optional diagnostic prefix prepended as `<prefix>: `.
 * @return Failed `TaskResult`.
 */
inline TaskResult make_failed_task_result(const std::string& message,
                                          const std::string& prefix = "") {
    const std::size_t prefix_end = message.find(": ");
    const bool message_has_error_prefix =
        prefix_end != std::string::npos &&
        message.substr(0, prefix_end).find("error") != std::string::npos;

    std::string result_message =
        prefix.empty() || message_has_error_prefix ? message : prefix + ": " + message;
    return TaskResult{TaskStatus::failed, std::move(result_message)};
}

/**
 * @brief Build a failed `TaskResult` from a captured exception.
 * @param exception Captured exception used to build the failure message.
 * @param prefix Optional diagnostic prefix prepended as `<prefix>: `.
 * @return Failed `TaskResult`.
 */
inline TaskResult make_failed_task_result(std::exception_ptr exception,
                                          const std::string& prefix = "") {
    if (!exception) {
        return make_failed_task_result("unknown exception", prefix);
    }

    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& caught_exception) {
        return make_failed_task_result(caught_exception.what(), prefix);
    } catch (...) {
        return make_failed_task_result("unknown exception", prefix);
    }

    return make_failed_task_result("unknown exception", prefix);
}

/**
 * @brief Runtime-local services available during `SimulationUnit::execute()`.
 *
 * `Runtime` dispatch constructs an instance for each task and calls `execute()`
 * with a runtime-local `ExecutionContext`.
 */
class ExecutionContext {
  public:
    /**
     * @brief Construct runtime services for one worker task execution.
     * @param actor_system Runtime CAF actor system used for actor communication.
     * @param active_subprocess Worker-owned subprocess state.
     */
    ExecutionContext(caf::actor_system& actor_system,
                     core::SubprocessState& active_subprocess);

    /**
     * @brief Execute a task through this worker's subprocess slot.
     * @param task Task to send to the subprocess worker.
     * @return Structured `TaskResult` from subprocess execution.
     */
    TaskResult execute_subprocess(const Task& task) const;

  private:
    // Runtime actor system borrowed for the current worker task execution
    caf::actor_system* actor_system_;

    // Worker-owned subprocess state borrowed for subprocess task execution
    core::SubprocessState* active_subprocess_;
};

/**
 * @brief Base class for model-specific simulation units.
 *
 * Implementations decide how to handle `Task::execution_mode_`. Subprocess
 * execution is available through `ExecutionContext::execute_subprocess()`.
 */
class SimulationUnit {
  public:
    virtual ~SimulationUnit() = default;

    /**
     * @brief Execute the simulation unit for a single task.
     * @param task Generic `Task` arguments.
     * @param context Runtime-local services for this worker task execution.
     * @return Structured `TaskResult` for this execution.
     */
    virtual TaskResult execute(const Task& task, ExecutionContext& context) = 0;
};

/**
 * @brief Factory used by each `simulation_unit_actor`.
 *
 * Workers use this to construct in-process `SimulationUnit` instances.
 */
using SimulationUnitFactory = std::function<std::unique_ptr<SimulationUnit>()>;

/**
 * @brief Registry for simulation unit factories.
 *
 * Tracks `simulation_unit_id -> factory` for runtime task routing.
 */
class SimulationUnitRegistry {
  public:
    SimulationUnitRegistry() = default;

    /**
     * @brief Register a concrete `SimulationUnit` type and return its assigned ID.
     *
     * Builds a factory that workers use for `SimulationUnitType` instances.
     *
     * @tparam SimulationUnitType Concrete `SimulationUnit` type.
     * @tparam ConstructorArgs Constructor argument types for `SimulationUnitType`.
     * @param constructor_args Arguments forwarded to `SimulationUnitType` constructor.
     * @return `SimulationUnitId` assigned to this registered unit.
     */
    template <class SimulationUnitType, class... ConstructorArgs>
        requires std::derived_from<SimulationUnitType, SimulationUnit> &&
                 std::constructible_from<SimulationUnitType,
                                         const std::decay_t<ConstructorArgs>&...>
    SimulationUnitId add(ConstructorArgs... constructor_args) {
        // Store constructor arguments in a tuple
        std::tuple<std::decay_t<ConstructorArgs>...> stored_constructor_args(
            std::move(constructor_args)...);

        // Build a factory for in-process simulation unit instances
        SimulationUnitFactory simulation_unit_factory =
            [stored_constructor_args =
                 std::move(stored_constructor_args)]() -> std::unique_ptr<SimulationUnit> {
            return std::apply(
                [](const std::decay_t<ConstructorArgs>&... args)
                    -> std::unique_ptr<SimulationUnit> {
                    return std::make_unique<SimulationUnitType>(args...);
                },
                stored_constructor_args);
        };

        const SimulationUnitId simulation_unit_id = next_simulation_unit_id_++;
        simulation_unit_factories_by_id_.emplace(simulation_unit_id,
                                                 std::move(simulation_unit_factory));
        return simulation_unit_id;
    }

    /**
     * @brief Return true when no simulation units have been registered.
     * @return true when the registry contains no simulation units.
     */
    bool empty() const {
        return simulation_unit_factories_by_id_.empty();
    }

    /**
     * @brief Look up factory for a simulation unit ID.
     * @param simulation_unit_id Registered ID to resolve.
     * @return Pointer to factory, or null when ID is unknown.
     */
    const SimulationUnitFactory* find_factory(SimulationUnitId simulation_unit_id) const {
        const std::unordered_map<SimulationUnitId, SimulationUnitFactory>::const_iterator
            simulation_unit_entry =
                simulation_unit_factories_by_id_.find(simulation_unit_id);
        if (simulation_unit_entry == simulation_unit_factories_by_id_.end()) {
            return nullptr;
        }
        return &simulation_unit_entry->second;
    }

  private:
    std::unordered_map<SimulationUnitId, SimulationUnitFactory>
        simulation_unit_factories_by_id_;
    SimulationUnitId next_simulation_unit_id_{1};
};

/**
 * @brief Runtime options for model-independent orchestration behaviour.
 *
 * Solver-specific options should be passed in `Task` arguments.
 */
struct RuntimeOptions {
    // Maximum number of worker actors that may run concurrently
    std::size_t max_workers_;

    // Timeout for a single task request from `job_actor` to `simulation_unit_actor`
    std::chrono::milliseconds task_request_timeout_;

    // Timeout for whole-job requests between `runtime`/`orchestrator`/`job_actor`
    std::chrono::milliseconds job_request_timeout_;

    // Root output directory where run/job artifact folders are created
    // When empty (the default), no output directories or files are written
    std::string output_root_;

    // Path to a NextGen-style realization file for realization-driven workflows
    // Empty string means no realization path has been configured
    std::string realization_path_;

    /**
     * @brief Construct runtime options with safe defaults.
     * @param workers Desired worker count. Values of zero are clamped to one.
     * @param task_timeout Timeout used for one simulation task request.
     * @param job_timeout Timeout used for whole-job requests.
     * @param output_root Root directory for runtime-managed output folders.
     *                   Empty string (default) disables all file output.
     * @param realization_path Path to a realization file. Empty string (default)
     *                   means no realization path has been configured.
     */
    RuntimeOptions(std::size_t workers = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::minutes(1),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5),
                   std::string output_root = "", std::string realization_path = "")
        : max_workers_(workers == 0 ? 1 : workers), task_request_timeout_(task_timeout),
          job_request_timeout_(job_timeout), output_root_(std::move(output_root)),
          realization_path_(std::move(realization_path)) {}
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result.
 */
using JobHandle = std::future<JobRecord>;

/**
 * @brief Runtime-owned registry of in-progress jobs.
 *
 * This is effectively a dictionary/hash map:
 * `job_id -> promise_for_that_job_record`.
 *
 * Each submitted `JobId` maps to the promise that will be fulfilled by the
 * orchestrator actor when that job finishes.
 */
using PendingJobs = std::unordered_map<JobId, std::promise<JobRecord>>;

/**
 * @brief Serialize or deserialize an `ExecutionMode` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param execution_mode `ExecutionMode` value to inspect.
 * @return true when the execution mode value was inspected successfully.
 */
template <class Inspector>
bool inspect(Inspector& inspector, ExecutionMode& execution_mode) {
    // Execution mode serializes as a stable string in actor messages and JSON
    std::string mode_value = "in_process";
    switch (execution_mode) {
    case ExecutionMode::in_process:
        break;
    case ExecutionMode::single_task_subprocess:
        mode_value = "single_task_subprocess";
        break;
    case ExecutionMode::persistent_subprocess:
        mode_value = "persistent_subprocess";
        break;
    }

    const bool inspected = inspector.apply(mode_value);
    if (!inspected) {
        return false;
    }

    if (mode_value == "single_task_subprocess") {
        execution_mode = ExecutionMode::single_task_subprocess;
    } else if (mode_value == "persistent_subprocess") {
        execution_mode = ExecutionMode::persistent_subprocess;
    } else {
        execution_mode = ExecutionMode::in_process;
    }
    return true;
}

/**
 * @brief Serialize or deserialize a `Task` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param task `Task` payload to inspect.
 * @return true when all fields were inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, Task& task) {
    // Defines how CAF serializes/deserializes `Task` for actor messaging
    return inspector.object(task).fields(
        inspector.field("simulation_unit_id", task.simulation_unit_id_),
        inspector.field("execution_mode", task.execution_mode_),
        inspector.field("subprocess_path", task.subprocess_path_),
        inspector.field("args", task.args_),
        inspector.field("task_output_dir", task.task_output_dir_),
        inspector.field("task_name", task.task_name_),
        inspector.field("metadata", task.metadata_));
}

/**
 * @brief Serialize or deserialize a `TaskStatus` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param task_status `TaskStatus` payload to inspect.
 * @return true when the status value was inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, TaskStatus& task_status) {
    // Task status serializes as a stable string in actor messages and JSON
    std::string status_value = task_status_to_string(task_status);
    const bool inspected = inspector.apply(status_value);
    if (!inspected) {
        return false;
    }
    task_status = status_value == "succeeded" ? TaskStatus::succeeded : TaskStatus::failed;
    return true;
}

/**
 * @brief Serialize or deserialize a `TaskResult` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param task_result `TaskResult` payload to inspect.
 * @return true when all fields were inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, TaskResult& task_result) {
    // Defines how CAF serializes/deserializes `TaskResult` for actor messaging
    return inspector.object(task_result)
        .fields(inspector.field("status", task_result.status_),
                inspector.field("message", task_result.message_),
                inspector.field("artifact_paths", task_result.artifact_paths_));
}

/**
 * @brief Serialize or deserialize a `TaskRecord` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param task_record `TaskRecord` payload to inspect.
 * @return true when all fields were inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, TaskRecord& task_record) {
    // Defines how CAF serializes/deserializes `TaskRecord` for actor messaging
    return inspector.object(task_record)
        .fields(inspector.field("task_id", task_record.task_id_),
                inspector.field("simulation_unit_id", task_record.simulation_unit_id_),
                inspector.field("task_name", task_record.task_name_),
                inspector.field("metadata", task_record.metadata_),
                inspector.field("result", task_record.result_),
                inspector.field("run_time_s", task_record.run_time_s_));
}

/**
 * @brief Serialize or deserialize a `JobRecord` for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param job_record `JobRecord` payload to inspect.
 * @return true when all fields were inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, JobRecord& job_record) {
    // Defines how CAF serializes/deserializes `JobRecord` for actor messaging
    return inspector.object(job_record)
        .fields(inspector.field("run_id", job_record.run_id_),
                inspector.field("job_id", job_record.job_id_),
                inspector.field("tasks", job_record.tasks_),
                inspector.field("run_time_s", job_record.run_time_s_));
}

/**
 * @brief Actor runtime for orchestrating simulation `Task`s.
 *
 * The runtime is model-agnostic and thread-safe.
 * Tasks with unknown `SimulationUnitId` values fail individually.
 * Persistent subprocess workers are owned by one submitted job.
 */
class Runtime {
  public:
    /**
     * @brief Construct `Runtime` from a registry with default options.
     * @param simulation_unit_registry Registry mapping `simulation_unit_id` to factory.
     * @throws std::invalid_argument if `simulation_unit_registry` is empty.
     */
    Runtime(const SimulationUnitRegistry& simulation_unit_registry);

    /**
     * @brief Construct `Runtime` from a registry and options.
     * @param simulation_unit_registry Registry mapping `simulation_unit_id` to factory.
     * @param options `Runtime` scheduling and timeout options.
     * @throws std::invalid_argument if `simulation_unit_registry` is empty.
     */
    Runtime(const SimulationUnitRegistry& simulation_unit_registry, RuntimeOptions options);

    /**
     * @brief Submit one `Task` as an asynchronous job.
     *
     * The returned handle may rethrow if final `job_record.json` output fails.
     * @param task Generic `Task` arguments.
     * @return `JobHandle` for retrieving the final `JobRecord`.
     * @throws std::filesystem::filesystem_error if runtime output directories
     * cannot be created.
     */
    JobHandle submit_job(const Task& task) const;

    /**
     * @brief Submit a multi-task asynchronous job.
     *
     * Lvalue inputs are copied, while rvalue inputs transfer ownership into
     * `Runtime`.
     * The returned handle may rethrow if final `job_record.json` output fails.
     * @param tasks Generic `Task` arguments.
     * @return `JobHandle` for retrieving the final `JobRecord`.
     * @throws std::filesystem::filesystem_error if runtime output directories
     * cannot be created.
     */
    JobHandle submit_job(std::vector<Task> tasks) const;

  private:
    // Shared simulation unit registry used by all `simulation_unit_actor`s in this runtime
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry_;

    // `RuntimeOptions` for this runtime, copied into each `job_actor` `JobState`
    RuntimeOptions runtime_options_;

    // Monotonic run ID used to create deterministic output directory layouts
    std::uint64_t run_id_{0};

    // Monotonic counter for generating unique `JobId`s for each submitted job
    mutable std::atomic<JobId> next_job_id_{0};

    // Mutex to protect access to the `pending_jobs_` dictionary
    mutable std::mutex pending_jobs_mutex_;

    // Dictionary mapping in-progress `JobId`s to their completion promises
    mutable PendingJobs pending_jobs_;

    // CAF actor system configuration for this runtime instance
    std::shared_ptr<caf::actor_system_config> actor_system_config_;

    // CAF actor system for this runtime instance
    std::shared_ptr<caf::actor_system> actor_system_;

    // Actor that receives requests from `Runtime`, spawns one `job_actor` per request,
    // and fulfills `Runtime` promises when jobs complete
    caf::actor orchestrator_;
};

} // namespace miaow
