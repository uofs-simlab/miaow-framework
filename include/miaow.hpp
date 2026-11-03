#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <deque>
#include <queue>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <caf/actor.hpp>
#include <caf/behavior.hpp>
#include <caf/disposable.hpp>
#include <caf/typed_response_promise.hpp>
#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/config_value.hpp>
#include <caf/result.hpp>
#include <caf/type_id.hpp>

namespace miaow {

class Job;
class SimulationUnit;

namespace core {
class SubprocessState;
} // namespace core

/**
 * @brief Monotonic identifier assigned to each registered `TaskExecutor`.
 */
using TaskExecutorId = std::uint64_t;

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
 * @brief Job-scoped reference to one declared persistent simulation unit.
 */
class SimulationUnitHandle {
  public:
    SimulationUnitHandle() = default;
    bool operator==(const SimulationUnitHandle&) const = default;

  private:
    SimulationUnitHandle(std::uint64_t scope_id, std::size_t index)
        : scope_id_(scope_id), index_(index) {}

    std::uint64_t scope_id_{0};
    std::size_t index_{0};

    friend class Job;
    template <class Inspector>
    friend bool inspect(Inspector& inspector, SimulationUnitHandle& unit) {
        return inspector.object(unit).fields(inspector.field("scope_id", unit.scope_id_),
                                             inspector.field("index", unit.index_));
    }
};

/**
 * @brief Input payload routed to one registered task executor.
 *
 * Arguments in `args_` are positional and interpreted by the concrete `TaskExecutor`
 * implementation inside `TaskExecutor::execute()`.
 */
struct Task {
    /// `TaskExecutorId` that selects which registered executor handles this task
    TaskExecutorId task_executor_id_{0};

    /// Named executor operation; ordinary executors may leave this empty
    std::string operation_;

    /// Persistent unit selected by a job; absent for ordinary tasks
    std::optional<SimulationUnitHandle> unit_;

    /// Generic arguments for this task to pass into `TaskExecutor::execute()`
    std::vector<caf::config_value> args_;

    /// Directory injected by `Runtime` where the executing `TaskExecutor` writes
    /// task artifacts
    std::string task_output_dir_;

    /// Artifact stem assigned by Runtime; persistent operations also include their task ID
    std::string task_name_;

    /// Metadata preserves labels used by reporting and postprocessing tools
    TaskMetadata metadata_;

    /// Construct an empty task
    Task() = default;

    /// Construct an operation for a persistent unit, including controller-generated work
    Task(SimulationUnitHandle unit, std::string operation,
         std::vector<caf::config_value> args = {})
        : operation_(std::move(operation)), unit_(unit), args_(std::move(args)) {}

    /// Convert positional operation arguments into config values
    template <class... Args>
    Task(SimulationUnitHandle unit, std::string operation, Args... args)
        : Task(unit, std::move(operation),
               std::vector<caf::config_value>{caf::config_value{std::move(args)}...}) {}

    /**
     * @brief Construct a `Task` from positional config values.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param task_args Positional arguments for this task.
     */
    Task(TaskExecutorId task_executor_id, std::vector<caf::config_value> task_args)
        : task_executor_id_(task_executor_id), args_(std::move(task_args)) {}

    /**
     * @brief Construct a `Task` from positional config values and metadata.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param task_args Positional arguments for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(TaskExecutorId task_executor_id, std::vector<caf::config_value> task_args,
         TaskMetadata task_metadata)
        : task_executor_id_(task_executor_id), args_(std::move(task_args)),
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` from one config value.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param arg Single `caf::config_value` for this task.
     */
    Task(TaskExecutorId task_executor_id, caf::config_value arg)
        : task_executor_id_(task_executor_id), args_{std::move(arg)} {}

    /**
     * @brief Construct a `Task` from one config value and metadata.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param arg Single `caf::config_value` for this task.
     * @param task_metadata Metadata preserved for reporting and postprocessing.
     */
    Task(TaskExecutorId task_executor_id, caf::config_value arg, TaskMetadata task_metadata)
        : task_executor_id_(task_executor_id), args_{std::move(arg)},
          metadata_(std::move(task_metadata)) {}

    /**
     * @brief Construct a `Task` from one string argument.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param arg Single string argument for this task.
     */
    Task(TaskExecutorId task_executor_id, std::string arg)
        : task_executor_id_(task_executor_id), args_{caf::config_value{std::move(arg)}} {}

    /**
     * @brief Construct a `Task` from positional string arguments.
     * @param task_executor_id `TaskExecutorId` for this task.
     * @param string_args Positional string arguments for this task.
     */
    Task(TaskExecutorId task_executor_id, std::initializer_list<std::string> string_args)
        : task_executor_id_(task_executor_id) {
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
 * @brief Result returned by one task execution.
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

    /// Task-executor status for this task execution
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
 * Every executable registered with `TaskExecutorRegistry::add_subprocess()` must call
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
     * @param task Submitted task whose metadata and task executor ID are copied.
     * @param task_id `TaskId` within the submitted job.
     * @param result Success or failure result for this task.
     * @param run_time Elapsed time from dispatch until the result or timeout is recorded.
     * @param dispatch_offset_from_job_start Time from job start to dispatch, if known.
     * @param worker_id Zero-based worker index within the job, if assigned.
     */
    TaskRecord(const Task& task, TaskId task_id, TaskResult result,
               std::chrono::duration<double> run_time,
               std::optional<std::chrono::duration<double>> dispatch_offset_from_job_start =
                   std::nullopt,
               std::optional<std::size_t> worker_id = std::nullopt)
        : task_id_{task_id}, task_executor_id_{task.task_executor_id_},
          task_name_{task.task_name_}, operation_{task.operation_}, unit_{task.unit_},
          metadata_{task.metadata_}, result_{std::move(result)}, run_time_{run_time},
          dispatch_offset_from_job_start_{dispatch_offset_from_job_start},
          worker_id_{worker_id} {}

    /// `TaskId` within the submitted job
    TaskId task_id_{0};

    /// `TaskExecutorId` that executed this task
    TaskExecutorId task_executor_id_{0};

    /// Task label copied from the corresponding `Runtime`-assigned `Task`
    std::string task_name_;

    /// Operation copied from the submitted task
    std::string operation_;

    /// Persistent target copied from the submitted task
    std::optional<SimulationUnitHandle> unit_;

    /// Metadata copied from the submitted task
    TaskMetadata metadata_;

    /// Task-executor result for this task
    TaskResult result_;

    /// Elapsed time from dispatch until the result or timeout is recorded
    std::chrono::duration<double> run_time_{};

    /// Time from job start to dispatch; absent if not dispatched or timing is unavailable
    std::optional<std::chrono::duration<double>> dispatch_offset_from_job_start_;

    /// Zero-based worker index within the job; absent if no worker assignment is available
    std::optional<std::size_t> worker_id_;
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
 * @brief Loss of one simulation unit's execution context.
 */
struct UnitFailure {
    SimulationUnitHandle unit_;
    std::string reason_;
};

/**
 * @brief Per-job policy for configuring units and responding to execution results.
 *
 * One controller belongs to each job and stays in the parent process. The job actor
 * serializes its hooks, so controller members accessed only by these hooks need no mutex.
 * Hooks should return promptly: blocking work delays results, shutdown, and job timeouts.
 *
 * Controllers add work through Job::add_task(), in task-ID order. The runtime prepares
 * and dispatches that work after the hook returns. With batch-synchronous policy, tasks
 * added during initialization form the first batch alongside caller-provided tasks;
 * tasks added by each callback form the next batch. Failed tasks count toward completion.
 */
class JobController {
  public:
    /// Select when the job actor calls callback()
    explicit JobController(
        TaskGenerationPolicy task_generation_policy = TaskGenerationPolicy::asynchronous)
        : task_generation_policy_{task_generation_policy} {}

    virtual ~JobController() = default;

    /// Task generation policy selected at construction
    TaskGenerationPolicy task_generation_policy() const {
        return task_generation_policy_;
    }

    /// Called once before dispatch, including when the caller supplied initial tasks
    virtual void initialize(Job&) {}

    /**
     * @brief Respond to completed tasks and unavailable simulation units.
     * @param job Job to which the controller may add units or tasks, or report failure.
     * @param completed_tasks New completions in completion order, delivered once per task.
     * @param failed_units Units whose execution context is unavailable.
     *
     * Spans are borrowed for this call and must not be retained. Asynchronous callbacks
     * receive each task result; batch-synchronous callbacks receive the completed batch.
     * The default policy fails the job on unit loss and permits reported task failures.
     * Synthetic job-timeout records remain only in the final JobRecord.
     */
    virtual void callback(Job& job, std::span<const TaskRecord> completed_tasks,
                          std::span<const UnitFailure> failed_units);

    /**
     * @brief Return structured output for the final `JobRecord`.
     *
     * Called once by the job actor on completion, including a job timeout or an empty
     * job. Calls are serialized with callback(), and this method should
     * return promptly. Implementations may move accumulated data into the result.
     * Task-generation errors and runtime shutdown produce no final record and do not call
     * this method. Exceptions propagate through `JobHandle::get()` as job errors.
     * @return Optional scalar, list, or dictionary output; null by default.
     */
    virtual caf::config_value finalize() {
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
     * @param output Structured output returned by the job controller.
     */
    JobRecord(std::uint64_t run_id, JobId job_id, std::vector<TaskRecord> tasks = {},
              std::chrono::duration<double> run_time = {}, caf::config_value output = {})
        : run_id_{run_id}, job_id_{job_id}, tasks_{std::move(tasks)}, run_time_{run_time},
          output_{std::move(output)} {}

    /// Runtime identifier for this `Runtime` instance
    std::uint64_t run_id_{0};

    /// `JobId` assigned to this submitted job
    JobId job_id_{0};

    /// Task records in submitted task order
    std::vector<TaskRecord> tasks_;

    /// Wall-clock duration for this job
    std::chrono::duration<double> run_time_{};

    /// Structured output returned by JobController::finalize(); null by default
    caf::config_value output_;
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
 * @brief Base class for model-specific task executors
 *
 * The only requirement of a `TaskExecutor` is that it must implement an `execute` method
 * that takes a `Task` and returns a `caf::result<TaskResult>`, which can be an immediate
 * result, a CAF error, or a delegated response. The `TaskExecutor` class is intended to
 * be subclassed by concrete implementations that define the specific behavior of the
 * task executor.
 */
class TaskExecutor {
  public:
    virtual ~TaskExecutor() = default;

    /**
     * @brief Execute the task executor for a single task.
     * @param task Generic `Task` arguments.
     * @return Immediate `TaskResult`, CAF error, or delegated response.
     */
    virtual caf::result<TaskResult> execute(const Task& task) = 0;
};

/**
 * @brief Registry of task executor implementations.
 *
 * Tracks `task_executor_id -> registration` mappings for runtime task routing.
 */
class TaskExecutorRegistry {
  public:
    TaskExecutorRegistry() = default;

    /**
     * @brief Register a concrete `TaskExecutor` type.
     *
     * Builds a factory that workers use for `TaskExecutorType` instances.
     *
     * @tparam TaskExecutorType Concrete `TaskExecutor` type.
     * @tparam ConstructorArgs Constructor argument types for `TaskExecutorType`.
     * @param constructor_args Arguments forwarded to `TaskExecutorType` constructor.
     * @return `TaskExecutorId` assigned to this registered executor.
     */
    template <class TaskExecutorType, class... ConstructorArgs>
        requires std::derived_from<TaskExecutorType, TaskExecutor> &&
                 std::constructible_from<TaskExecutorType,
                                         const std::decay_t<ConstructorArgs>&...>
    TaskExecutorId add(ConstructorArgs... constructor_args) {
        // Store constructor arguments in a tuple
        std::tuple<std::decay_t<ConstructorArgs>...> stored_constructor_args(
            std::move(constructor_args)...);

        // Build a factory for in-process task executor instances
        TaskExecutorFactory task_executor_factory =
            [stored_constructor_args =
                 std::move(stored_constructor_args)]() -> std::unique_ptr<TaskExecutor> {
            return std::apply(
                [](const std::decay_t<ConstructorArgs>&... args)
                    -> std::unique_ptr<TaskExecutor> {
                    return std::make_unique<TaskExecutorType>(args...);
                },
                stored_constructor_args);
        };

        const TaskExecutorId task_executor_id = next_task_executor_id_++;
        registrations_.emplace(task_executor_id, std::move(task_executor_factory));
        return task_executor_id;
    }

    /**
     * @brief Register a subprocess task executor.
     *
     * The executable path cannot contain command-line arguments because MIAOW supplies
     * the arguments required for parent registration.
     *
     * @param subprocess_executable Non-empty path to an executable whose `main()` calls
     * `run_subprocess_worker()`.
     * @param lifetime Select a single-task or persistent child process.
     * @return `TaskExecutorId` assigned to this subprocess task executor.
     * @throws std::invalid_argument if `subprocess_executable` is empty.
     */
    TaskExecutorId add_subprocess(std::string subprocess_executable,
                                  SubprocessLifetime lifetime);

    /**
     * @brief Return true when no task executors have been registered.
     * @return true when the registry contains no task executors.
     */
    bool empty() const {
        return registrations_.empty();
    }

  private:
    // Query subprocess execution so the job actor can select timeout ownership
    bool is_subprocess(TaskExecutorId task_executor_id) const;

    // Create an executor with actor-owned lifecycle state for subprocess execution
    std::unique_ptr<TaskExecutor>
    make_executor(TaskExecutorId task_executor_id,
                  core::SubprocessState& subprocess_state) const;

    using TaskExecutorFactory = std::function<std::unique_ptr<TaskExecutor>()>;

    struct SubprocessRegistration {
        std::string executable_;
        SubprocessLifetime lifetime_;
    };

    using Registration = std::variant<TaskExecutorFactory, SubprocessRegistration>;

    std::unordered_map<TaskExecutorId, Registration> registrations_;
    TaskExecutorId next_task_executor_id_{1};

    friend class Job;
    friend class SimulationUnit;
};

/**
 * @brief Runtime options for model-independent orchestration behaviour.
 *
 * Solver-specific options should be passed in `Task` arguments.
 */
struct RuntimeOptions {
    /// Maximum number of dispatched tasks awaiting completion in each job
    std::size_t max_concurrent_tasks_;

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
     * @param concurrent_tasks Maximum active task requests for each job. Values of zero are
     *                clamped to one.
     * @param task_timeout Task-response timeout; terminates only subprocess tasks.
     * @param job_timeout Timeout that fails unfinished tasks and requests worker shutdown.
     * @param output_root Root directory for runtime-managed output folders.
     *                   Empty string (default) disables all file output.
     */
    RuntimeOptions(std::size_t concurrent_tasks = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::minutes(1),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5),
                   std::string output_root = "")
        : max_concurrent_tasks_(concurrent_tasks == 0 ? 1 : concurrent_tasks),
          task_request_timeout_(task_timeout), job_request_timeout_(job_timeout),
          output_root_(std::move(output_root)) {}
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result. It throws
 * `std::runtime_error` when the owning job actor reports a job-level failure.
 */
using JobHandle = std::future<JobRecord>;

/**
 * @brief Simulation-unit state prepared before submission and managed by CAF afterward.
 *
 * The selected executor is constructed when the actor starts and retained across tasks.
 */
class SimulationUnit {
  public:
    explicit SimulationUnit(TaskExecutorId executor_id);
    ~SimulationUnit();
    SimulationUnit(SimulationUnit&&) noexcept;
    SimulationUnit& operator=(SimulationUnit&&) noexcept;
    SimulationUnit(const SimulationUnit&) = delete;
    SimulationUnit& operator=(const SimulationUnit&) = delete;

    /// @cond
    // CAF supplies context to prepared units and to ordinary per-task workers
    SimulationUnit(caf::event_based_actor* self,
                   std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
                   caf::actor subprocess_registration_actor,
                   std::uint16_t subprocess_registration_port,
                   std::chrono::milliseconds task_request_timeout);
    SimulationUnit(caf::event_based_actor* self, SimulationUnit&& unit,
                   std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
                   caf::actor subprocess_registration_actor,
                   std::uint16_t subprocess_registration_port,
                   std::chrono::milliseconds task_request_timeout);
    caf::behavior make_behavior();
    /// @endcond

  private:
    SimulationUnit() = default;

    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;
    std::unique_ptr<core::SubprocessState> subprocess_state_;
    std::optional<TaskExecutorId> persistent_executor_id_;
    std::unique_ptr<TaskExecutor> task_executor_;
};

/**
 * @brief A move-only job configured before submission and managed by CAF during execution.
 *
 * The job owns its simulation units, initial tasks, and controller. Handles remain valid
 * across moves and may be retained by the controller for subsequent operations.
 */
class Job {
  public:
    Job();
    ~Job();
    Job(Job&&);
    Job& operator=(Job&&);
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    /// Construct one persistent simulation unit under a unique scientific feature label
    SimulationUnitHandle add_unit(std::string feature_id, TaskExecutorId executor_id);

    /// Append a task before submission or during a controller hook
    void add_task(Task task);

    /// Append an operation using the Task argument constructors
    template <class... Args>
    void add_task(SimulationUnitHandle unit, std::string operation, Args&&... args) {
        add_task(Task{unit, std::move(operation), std::forward<Args>(args)...});
    }

    /// Report a job-level error from a running controller hook
    void fail(std::string reason);

    /// Transfer controller ownership to the job before submission
    void set_controller(std::unique_ptr<JobController> controller);

    /// @cond
    // CAF supplies actor context while adopting the prepared job state
    Job(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
        std::shared_ptr<Job> job, RuntimeOptions runtime_options,
        std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
        caf::actor subprocess_registration_actor,
        std::uint16_t subprocess_registration_port);
    caf::behavior make_behavior();
    /// @endcond

  private:
    void stop_workers();
    void spawn_workers_for_tasks();
    void prepare_tasks(std::size_t first_task_index);
    void complete_job();
    void complete_job_error_if_tasks_finished();
    void begin_job_error_shutdown(std::string error_message);
    void complete_job_if_all_tasks_finished();
    void complete_task(std::size_t worker_id, std::size_t task_id, TaskResult task_result);
    void dispatch_tasks();

    caf::event_based_actor* self_{nullptr};

    // Runtime identifier copied into the final job record
    std::uint64_t run_id_{0};

    // Job identifier copied into the final job record
    JobId job_id_{0};

    // Runtime options for this job, copied from the runtime config
    RuntimeOptions runtime_options_;

    // Controller configured before submission and retained for this job
    // The task generation policy determines when the job actor calls callback()
    // The controller appends tasks through this job and stays in the parent process
    std::unique_ptr<JobController> job_controller_{std::make_unique<JobController>()};

    // Registry retained so workers can be created for controller-added tasks
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;

    // Runtime-local registrar shared by every subprocess-capable worker in this job
    caf::actor subprocess_registration_actor_;

    // Loopback port used by child actor systems to reach the registration actor
    std::uint16_t subprocess_registration_port_{0};

    struct UnitBinding {
        std::string feature_id_;
        TaskExecutorId task_executor_id_;
        std::variant<SimulationUnit, caf::actor> instance_;
        std::optional<TaskId> active_task_;
    };

    // Unit identities remain available for routing after CAF adopts the prepared units
    inline static std::atomic<std::uint64_t> next_scope_id_{1};
    std::uint64_t scope_id_{next_scope_id_.fetch_add(1, std::memory_order_relaxed)};
    std::vector<std::optional<UnitBinding>> units_;

    void bind_tasks_to_simulation_units(std::vector<Task>& tasks,
                                        std::size_t first_task_index) const;

    // Initial and controller-generated task payloads owned by this job actor
    // Task IDs are the index of the task in this vector
    std::vector<Task> tasks_;

    // Successful and failed task records stored in completion order
    // Controllers borrow a view of new records during each callback
    // The records are sorted by task ID when the final JobRecord is constructed
    std::vector<TaskRecord> completed_task_records_;

    // Boundary between records already delivered to the controller and new completions
    std::size_t reported_task_count_{0};

    // Ordinary worker-pool actors, indexed by worker ID
    std::vector<caf::actor> workers_;

    // Queue of worker IDs that are available to run a task
    std::queue<std::size_t> available_workers_;

    // Pending task IDs remain in submission order; busy persistent targets may be skipped
    std::deque<std::size_t> pending_tasks_;

    // Number of dispatched task requests awaiting results
    std::size_t active_task_count_{0};

    // Job-level error reported after all active task requests finish
    std::optional<std::string> pending_error_message_;

    // Response promise used to deliver the final JobRecord or a job-level error
    std::optional<caf::typed_response_promise<JobRecord>> response_promise_;

    // Delayed message that tells the job actor when its configured timeout expires
    caf::disposable job_timeout_;

    // Wall-clock start time for the full job
    std::chrono::steady_clock::time_point job_start_time_;

    // Start times for each task, indexed by task ID
    std::vector<std::chrono::steady_clock::time_point> task_start_times_;

    // Worker indices assigned at dispatch, indexed by task ID; queued tasks have no worker
    std::vector<std::optional<std::size_t>> task_worker_ids_;

    friend class Runtime;
};

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
        inspector.field("task_executor_id", task.task_executor_id_),
        inspector.field("operation", task.operation_).fallback(std::string{}),
        inspector.field("unit", task.unit_).fallback(std::optional<SimulationUnitHandle>{}),
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
        .fields(
            inspector.field("task_id", task_record.task_id_),
            inspector.field("task_executor_id", task_record.task_executor_id_),
            inspector.field("task_name", task_record.task_name_),
            inspector.field("operation", task_record.operation_).fallback(std::string{}),
            inspector.field("unit", task_record.unit_)
                .fallback(std::optional<SimulationUnitHandle>{}),
            inspector.field("metadata", task_record.metadata_),
            inspector.field("result", task_record.result_),
            inspector.field("run_time", task_record.run_time_),
            inspector.field("dispatch_offset_from_job_start",
                            task_record.dispatch_offset_from_job_start_),
            inspector.field("worker_id", task_record.worker_id_));
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
        .fields(
            inspector.field("run_id", job_record.run_id_),
            inspector.field("job_id", job_record.job_id_),
            inspector.field("tasks", job_record.tasks_),
            inspector.field("run_time", job_record.run_time_),
            inspector.field("output", job_record.output_).fallback(caf::config_value{}));
}

/**
 * @brief Actor runtime for orchestrating simulation `Task`s.
 *
 * The runtime is model-agnostic and thread-safe.
 * Ordinary tasks with unknown `TaskExecutorId` values fail individually. Persistent
 * executor initialization errors fail the job.
 * Persistent subprocess workers are owned by one submitted job.
 */
class Runtime {
  public:
    /**
     * @brief Construct `Runtime` from a registry with default options.
     * @param task_executor_registry Registry mapping `task_executor_id` to factory.
     * @throws std::invalid_argument if `task_executor_registry` is empty.
     */
    Runtime(const TaskExecutorRegistry& task_executor_registry);

    /**
     * @brief Construct `Runtime` from a registry and options.
     * @param task_executor_registry Registry mapping `task_executor_id` to factory.
     * @param options `Runtime` scheduling and timeout options.
     * @throws std::invalid_argument if `task_executor_registry` is empty.
     */
    Runtime(const TaskExecutorRegistry& task_executor_registry, RuntimeOptions options);

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
     * @brief Submit a controller that generates its own initial tasks at job start.
     *
     * Calls initialize(Job&) once in the job actor. Controller hooks are
     * serialized, and their errors propagate through JobHandle::get().
     * @param job_controller Controller transferred to the submitted job.
     * @return Handle for retrieving the final JobRecord.
     * @throws std::invalid_argument if job_controller is null.
     */
    JobHandle submit_job(std::unique_ptr<JobController> job_controller) const;

    /**
     * @brief Submit tasks with a controller that may generate additional work.
     *
     * The runtime takes ownership of `job_controller`. After each task execution
     * returns success or failure, the owning job actor creates its `TaskRecord`. When
     * the controller's task generation policy permits, it passes new completions to
     * the controller. The controller appends further work through Job::add_task().
     * The initialize(Job&) hook runs once before dispatch, including when the
     * supplied task vector is empty.
     * The controller's finalize() output is retained in `JobRecord::output_`.
     *
     * If the controller or preparation of its added tasks fails, the job stops
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

    /**
     * @brief Transfer a configured job to the runtime and create its CAF actors.
     *
     * The controller's initialize(Job&) hook runs once before dispatch and may add
     * work alongside caller-provided tasks. Controller errors
     * propagate through JobHandle::get().
     * @param job Job whose units, tasks, and controller are transferred to the runtime.
     * @return Handle for retrieving the completed job record.
     * @throws std::invalid_argument if the controller is null or a task targets another
     * job.
     */
    JobHandle submit_job(Job job) const;

  private:
    // Map each submitted job to the promise fulfilled by the orchestrator
    using PendingJobs = std::unordered_map<JobId, std::promise<JobRecord>>;

    // Shared task executor registry used by all `simulation_unit_actor`s in this runtime
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;

    // Runtime options copied into each job actor's Job
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
