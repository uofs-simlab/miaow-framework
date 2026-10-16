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
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <caf/actor.hpp>
#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/config_value.hpp>
#include <caf/result.hpp>
#include <caf/type_id.hpp>

namespace miaow {

namespace core {
class SimulationUnitRegistryAccess;
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
 * @brief Select whether a subprocess handles one task or remains available for reuse.
 */
enum class SubprocessLifetime {
    /// Stop the subprocess after one task result
    single_task,

    /// Retain the subprocess after successful tasks
    persistent,
};

/**
 * @brief Input payload routed to one registered simulation unit.
 *
 * Arguments in `args_` are positional and interpreted by the concrete `SimulationUnit`
 * implementation inside `SimulationUnit::execute()`.
 */
struct Task {
    /// `SimulationUnitId` that selects which registered unit handles this task
    SimulationUnitId simulation_unit_id_{0};

    /// Generic arguments for this task to pass into `SimulationUnit::execute()`
    std::vector<caf::config_value> args_;

    /// Directory injected by `Runtime` where the executing `SimulationUnit` writes
    /// task artifacts
    std::string task_output_dir_;

    /// Task label injected by `Runtime` from non-empty `feature_id` metadata or the
    /// task index and used as an artifact file stem
    std::string task_name_;

    /// Metadata preserves labels used by reporting and postprocessing tools
    TaskMetadata metadata_;

    /// Construct an empty task
    Task() = default;

    /**
     * @brief Construct a `Task` from positional config values.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param task_args Positional arguments for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::vector<caf::config_value> task_args)
        : simulation_unit_id_(task_simulation_unit_id), args_(std::move(task_args)) {}

    /**
     * @brief Construct a `Task` from positional config values and metadata.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param task_args Positional arguments for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::vector<caf::config_value> task_args,
         TaskMetadata task_metadata)
        : simulation_unit_id_(task_simulation_unit_id), args_(std::move(task_args)),
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` from one config value.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single `caf::config_value` for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, caf::config_value arg)
        : simulation_unit_id_(task_simulation_unit_id), args_{std::move(arg)} {}

    /**
     * @brief Construct a `Task` from one config value and metadata.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single `caf::config_value` for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(SimulationUnitId task_simulation_unit_id, caf::config_value arg,
         TaskMetadata task_metadata)
        : simulation_unit_id_(task_simulation_unit_id), args_{std::move(arg)},
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` from one string argument.
     * @param task_simulation_unit_id `SimulationUnitId` for this task.
     * @param arg Single string argument for this task.
     */
    Task(SimulationUnitId task_simulation_unit_id, std::string arg)
        : simulation_unit_id_(task_simulation_unit_id),
          args_{caf::config_value{std::move(arg)}} {}

    /**
     * @brief Construct a `Task` from positional string arguments.
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
enum class TaskStatus : std::uint8_t {
    /// Task execution completed successfully
    succeeded = 0,

    /// Task execution terminated with a failure
    failed = 1,
};

/**
 * @brief Convert a `TaskStatus` to its lowercase string value.
 * @param status `TaskStatus` enum value.
 * @return `"succeeded"` or `"failed"`, according to `status`.
 */
inline std::string task_status_to_string(TaskStatus status) {
    if (status == TaskStatus::succeeded) {
        return "succeeded";
    }
    return "failed";
}

/**
 * @brief `TaskResult` returned by one simulation unit task execution.
 *
 * `output_` stores optional output data as a scalar, list, dictionary, or
 * recursively nested combination. Large or independently consumable files remain
 * artifacts referenced by `artifact_paths_`.
 */
struct TaskResult {
    /// Construct an empty failed task result
    TaskResult() = default;

    /**
     * @brief Construct a `TaskResult` from status, message, and artifact paths.
     * @param status Terminal status for this task execution.
     * @param message Human-readable completion or failure detail.
     * @param artifact_paths Relative paths to artifacts under the job directory.
     */
    TaskResult(TaskStatus status, std::string message,
               std::vector<std::string> artifact_paths = {})
        : status_{status}, message_{std::move(message)},
          artifact_paths_{std::move(artifact_paths)} {}

    /**
     * @brief Construct a `TaskResult` from status, message, output, and artifacts.
     * @param status Terminal status for this task execution.
     * @param message Human-readable completion or failure detail.
     * @param output Structured output for controllers and callers.
     * @param artifact_paths Relative paths to artifacts under the job directory.
     */
    TaskResult(TaskStatus status, std::string message, caf::config_value output,
               std::vector<std::string> artifact_paths = {})
        : status_{status}, message_{std::move(message)}, output_{std::move(output)},
          artifact_paths_{std::move(artifact_paths)} {}

    /// Simulation-unit status for this task execution
    TaskStatus status_{TaskStatus::failed};

    /// Message interpreted by `status_` as output or failure detail
    std::string message_;

    /// Structured output available to the JobController and runtime caller
    caf::config_value output_;

    /// Relative paths to task artifacts produced under the job directory
    std::vector<std::string> artifact_paths_;
};

/**
 * @brief Run a subprocess worker that handles serialized tasks from its parent.
 *
 * Every executable registered with `SimulationUnitRegistry::add_subprocess()` must call
 * this function once from its `main()` and pass the supplied `argc` and `argv` unchanged.
 * The handler runs serially for each received `Task`, and a persistent subprocess may
 * invoke the same handler more than once. An exception from the handler produces a failed
 * `TaskResult`.
 *
 * @param argc Command-line argument count supplied to the subprocess executable.
 * @param argv Command-line arguments supplied to the subprocess executable.
 * @param task_handler Callable that executes one received `Task` and returns its
 * `TaskResult`.
 * @return Zero after orderly worker shutdown or one after setup failure.
 */
int run_subprocess_worker(int argc, char* argv[],
                          std::function<TaskResult(const Task&)> task_handler);

/**
 * @brief Runtime `TaskRecord` for one completed task.
 *
 * A task is completed when its result status is `TaskStatus::succeeded` or
 * `TaskStatus::failed`.
 */
struct TaskRecord {
    /// Construct an empty task record
    TaskRecord() = default;

    /**
     * @brief Construct a completed `TaskRecord` from a task and its success or failure.
     * @param task Submitted task whose metadata and simulation unit ID are copied.
     * @param task_id `TaskId` within the submitted job.
     * @param result Success or failure result for this task.
     * @param run_time Elapsed time from dispatch until the result or timeout is recorded.
     * @param dispatch_offset_from_job_start Time from job start to dispatch, if known.
     */
    TaskRecord(const Task& task, TaskId task_id, TaskResult result,
               std::chrono::duration<double> run_time,
               std::optional<std::chrono::duration<double>> dispatch_offset_from_job_start =
                   std::nullopt)
        : task_id_{task_id}, simulation_unit_id_{task.simulation_unit_id_},
          task_name_{task.task_name_}, metadata_{task.metadata_},
          result_{std::move(result)}, run_time_{run_time},
          dispatch_offset_from_job_start_{dispatch_offset_from_job_start} {}

    /// `TaskId` within the submitted job
    TaskId task_id_{0};

    /// `SimulationUnitId` that executed this task
    SimulationUnitId simulation_unit_id_{0};

    /// Task label copied from the corresponding `Runtime`-assigned `Task`
    std::string task_name_;

    /// Metadata copied from the submitted task
    TaskMetadata metadata_;

    /// Simulation-unit result for this task
    TaskResult result_;

    /// Elapsed time from dispatch until the result or timeout is recorded
    std::chrono::duration<double> run_time_{};

    /// Time from job start to dispatch; absent if not dispatched or timing is unavailable
    std::optional<std::chrono::duration<double>> dispatch_offset_from_job_start_;
};

/**
 * @brief Select when a job controller generates tasks from completed records.
 */
enum class TaskGenerationPolicy {
    /// Call the controller after each task reports success or failure
    asynchronous,

    /// Call the controller when no queued tasks remain to dispatch and no dispatched
    /// tasks are awaiting results
    batch_synchronous,
};

/**
 * @brief Per-job policy for generating tasks from completed task records.
 *
 * One controller instance belongs to each submitted job and remains in the parent
 * process that owns `Runtime`. Only the owning job actor calls `generate_new_tasks()`.
 * Calls are serialized even when multiple workers finish concurrently, so controller
 * members accessed only by this method do not require a mutex. Results from workers
 * that finish concurrently may be processed in either order.
 *
 * `generate_new_tasks()` runs synchronously and should return promptly. Long-running or
 * blocking work delays queued task results, shutdown requests, and the job timeout.
 *
 * Tasks returned by one call are appended to the job in vector order. The first
 * returned task receives the next available `TaskId`, and the remaining tasks receive
 * consecutive `TaskId` values. Before dispatch, returned tasks undergo the same
 * runtime-field assignment and duplicate-name validation as initial tasks.
 * With `TaskGenerationPolicy::batch_synchronous`, the initial tasks form the first batch
 * and each returned vector forms the next batch. Failed tasks, including task timeouts,
 * count toward batch completion; the controller decides how to handle their results.
 */
class JobController {
  public:
    /// Select when the job actor calls generate_new_tasks()
    explicit JobController(
        TaskGenerationPolicy task_generation_policy = TaskGenerationPolicy::asynchronous)
        : task_generation_policy_{task_generation_policy} {}

    virtual ~JobController() = default;

    /// Task generation policy selected at construction
    TaskGenerationPolicy task_generation_policy() const {
        return task_generation_policy_;
    }

    /**
     * @brief Generate tasks when the selected task generation policy permits.
     * @param completed_task_records All completed task records in completion order.
     * @return Tasks to append to the current job in the order returned.
     */
    virtual std::vector<Task>
    generate_new_tasks(const std::vector<TaskRecord>& completed_task_records) {
        static_cast<void>(completed_task_records);
        return {};
    }

  private:
    TaskGenerationPolicy task_generation_policy_;
};

/**
 * @brief Runtime `JobRecord` returned for one submitted job.
 */
struct JobRecord {
    /// Construct an empty job record
    JobRecord() = default;

    /**
     * @brief Construct a final `JobRecord` from IDs, task records, and runtime.
     * @param run_id Runtime identifier for this `Runtime` instance.
     * @param job_id `JobId` assigned to this submitted job.
     * @param tasks Task records in submitted task order.
     * @param run_time Wall-clock duration for this job.
     */
    JobRecord(std::uint64_t run_id, JobId job_id, std::vector<TaskRecord> tasks = {},
              std::chrono::duration<double> run_time = {})
        : run_id_{run_id}, job_id_{job_id}, tasks_{std::move(tasks)},
          run_time_{run_time} {}

    /// Runtime identifier for this `Runtime` instance
    std::uint64_t run_id_{0};

    /// `JobId` assigned to this submitted job
    JobId job_id_{0};

    /// Task records in submitted task order
    std::vector<TaskRecord> tasks_;

    /// Wall-clock duration for this job
    std::chrono::duration<double> run_time_{};
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
 * @brief Base class for model-specific simulation units 
 * 
 * The only requirement of a `SimulationUnit` is that it must implement an `execute` method 
 * that takes a `Task` and returns a `caf::result<TaskResult>`, which can be an immediate 
 * result, a CAF error, or a delegated response. The `SimulationUnit` class is intended to 
 * be subclassed by concrete implementations that define the specific behavior of the 
 * simulation unit.
 */
class SimulationUnit {
  public:
    virtual ~SimulationUnit() = default;

    /**
     * @brief Execute the simulation unit for a single task.
     * @param task Generic `Task` arguments.
     * @return Immediate `TaskResult`, CAF error, or delegated response.
     */
    virtual caf::result<TaskResult> execute(const Task& task) = 0;
};

/**
 * @brief Registry of simulation unit implementations.
 *
 * Tracks `simulation_unit_id -> registration` mappings for runtime task routing.
 */
class SimulationUnitRegistry {
  public:
    SimulationUnitRegistry() = default;

    /**
     * @brief Register a concrete `SimulationUnit` type.
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
        registrations_.emplace(simulation_unit_id, std::move(simulation_unit_factory));
        return simulation_unit_id;
    }

    /**
     * @brief Register a subprocess simulation unit.
     *
     * The executable path cannot contain command-line arguments because MIAOW supplies
     * the arguments required for parent registration.
     *
     * @param subprocess_executable Non-empty path to an executable whose `main()` calls
     * `run_subprocess_worker()`.
     * @param lifetime Select a single-task or persistent child process.
     * @return `SimulationUnitId` assigned to this subprocess simulation unit.
     * @throws std::invalid_argument if `subprocess_executable` is empty.
     */
    SimulationUnitId add_subprocess(std::string subprocess_executable,
                                    SubprocessLifetime lifetime);

    /**
     * @brief Return true when no simulation units have been registered.
     * @return true when the registry contains no simulation units.
     */
    bool empty() const {
        return registrations_.empty();
    }

  private:
    using SimulationUnitFactory = std::function<std::unique_ptr<SimulationUnit>()>;

    struct SubprocessRegistration {
        std::string executable_;
        SubprocessLifetime lifetime_;
    };

    using Registration = std::variant<SimulationUnitFactory, SubprocessRegistration>;

    std::unordered_map<SimulationUnitId, Registration> registrations_;
    SimulationUnitId next_simulation_unit_id_{1};

    friend class core::SimulationUnitRegistryAccess;
};

/**
 * @brief Runtime options for model-independent orchestration behaviour.
 *
 * Solver-specific options should be passed in `Task` arguments.
 */
struct RuntimeOptions {
    /// Maximum number of worker actors used for each job
    std::size_t max_workers_;

    /// Timeout for one task response; expiry marks the task as failed. Subprocess tasks
    /// are terminated; in-process execute() calls are not interrupted.
    std::chrono::milliseconds task_request_timeout_;

    /// Job timeout; expiry marks unfinished tasks as failed and requests worker shutdown.
    /// Only subprocess tasks are terminated; in-process calls are not interrupted.
    std::chrono::milliseconds job_request_timeout_;

    /// Root output directory for run and job artifact folders; an empty value disables
    /// all output directories and files.
    std::string output_root_;

    /**
     * @brief Construct runtime options.
     * @param workers Maximum number of worker actors used for each job. Values of zero are
     *                clamped to one.
     * @param task_timeout Task-response timeout; terminates only subprocess tasks.
     * @param job_timeout Timeout that fails unfinished tasks and requests worker shutdown.
     * @param output_root Root directory for runtime-managed output folders.
     *                   Empty string (default) disables all file output.
     */
    RuntimeOptions(std::size_t workers = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::minutes(1),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5),
                   std::string output_root = "")
        : max_workers_(workers == 0 ? 1 : workers), task_request_timeout_(task_timeout),
          job_request_timeout_(job_timeout), output_root_(std::move(output_root)) {}
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result. It throws
 * `std::runtime_error` when the owning job actor reports a job-level failure.
 */
using JobHandle = std::future<JobRecord>;

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
    // Task status uses "succeeded" or "failed" in actor messages and JSON
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
        .fields(
            inspector.field("status", task_result.status_),
            inspector.field("message", task_result.message_),
            inspector.field("output", task_result.output_).fallback(caf::config_value{}),
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
                inspector.field("run_time", task_record.run_time_),
                inspector.field("dispatch_offset_from_job_start",
                                task_record.dispatch_offset_from_job_start_));
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
                inspector.field("run_time", job_record.run_time_));
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

    /// Request actor shutdown before releasing the shared CAF actor system
    ~Runtime();

    /**
     * @brief Submit one `Task` as an asynchronous job.
     *
     * The returned handle may throw if job execution or final `job_record.json` output
     * fails.
     * @param task Generic `Task` arguments.
     * @return `JobHandle` for retrieving the final `JobRecord`.
     * @throws std::filesystem::filesystem_error if runtime output directories
     * cannot be created.
     */
    JobHandle submit_job(const Task& task) const;

    /**
     * @brief Submit multiple tasks and return immediately with a `JobHandle`.
     *
     * Call `JobHandle::get()` to wait for the tasks to finish and obtain their
     * `JobRecord`.
     *
     * Passing an already-created task vector copies the vector and its tasks,
     * leaving the original unchanged. Passing a temporary task vector, such as the
     * return value of a function call, or using `std::move` allows MIAOW to reuse
     * the vector's existing storage and tasks instead of copying them. Do not rely
     * on the contents of an explicitly moved vector afterward.
     *
     * The returned handle may throw if job execution or final `job_record.json` output
     * fails.
     * @param tasks Tasks to submit.
     * @return `JobHandle` used to wait for and retrieve the resulting `JobRecord`.
     * @throws std::filesystem::filesystem_error if runtime output directories
     * cannot be created.
     */
    JobHandle submit_job(std::vector<Task> tasks) const;

    /**
     * @brief Submit tasks with a controller that may generate additional work.
     *
     * The runtime takes ownership of `job_controller`. After each task execution
     * returns success or failure, the owning job actor creates its `TaskRecord`. When
     * the controller's task generation policy permits, it passes all completed task
     * records to the controller and appends any returned tasks to the job.
     * An empty `tasks` vector returns an empty `JobRecord` without invoking
     * `job_controller`.
     *
     * If the controller or preparation of its returned tasks fails, the job stops
     * dispatching pending tasks and allows active tasks to finish. The returned handle
     * then throws `std::runtime_error`. No final `JobRecord` or `job_record.json` is
     * produced for this job-level error. Output failures may also be rethrown by the
     * handle.
     * @param tasks Initial tasks to submit.
     * @param job_controller Controller transferred to the submitted job.
     * @return `JobHandle` used to wait for and retrieve the resulting `JobRecord`.
     * @throws std::invalid_argument if `job_controller` is null.
     * @throws std::filesystem::filesystem_error if runtime output directories
     * cannot be created.
     */
    JobHandle submit_job(std::vector<Task> tasks,
                         std::unique_ptr<JobController> job_controller) const;

  private:
    // Map each submitted job to the promise fulfilled by the orchestrator
    using PendingJobs = std::unordered_map<JobId, std::promise<JobRecord>>;

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

// Register the public types carried by MIAOW actor messages
CAF_BEGIN_TYPE_ID_BLOCK(miaow_module, caf::first_custom_type_id)
CAF_ADD_TYPE_ID(miaow_module, (miaow::Task))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskResult))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskRecord))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::TaskRecord>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::Task>))
CAF_ADD_TYPE_ID(miaow_module, (miaow::JobRecord))
CAF_END_TYPE_ID_BLOCK(miaow_module)
