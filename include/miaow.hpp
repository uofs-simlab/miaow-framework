#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
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
 * @brief Zero-based task index within one job; callers supply the owning job.
 */
using TaskId = std::uint64_t;

/**
 * @brief Zero-based unit index within one job; IDs are never reused within that job.
 */
using SimulationUnitId = std::uint64_t;

/**
 * @brief Runtime-local identifier assigned when a job is submitted.
 */
using JobId = std::uint64_t;

/**
 * @brief Task output to substitute for a positional operation argument.
 *
 * The wrapper is used to allow for overloading in task constructors using
 * `output_of(producer)`.
 */
struct TaskOutputReference {
    TaskId task_id_;
};

/// Reference an output whose producer must also be explicitly listed as a dependency
inline TaskOutputReference output_of(TaskId producer) {
    return TaskOutputReference{producer};
}

/**
 * @brief Input payload routed to one registered task executor.
 *
 * Arguments in `args_` are positional and interpreted by the concrete `TaskExecutor`
 * implementation inside `TaskExecutor::execute()`.
 */
struct Task {
    /// Named operation interpreted by the unit's executor
    std::string operation_;

    /// Job-scoped unit that executes this operation
    SimulationUnitId unit_id_{0};

    /// Generic arguments for this task to pass into `TaskExecutor::execute()`
    std::vector<caf::config_value> args_;

    /// Parent-job scheduling data; only resolved arguments are sent to executors
    std::vector<TaskId> dependencies_;
    std::map<std::size_t, TaskId> output_references_;

    /// Artifact directory supplied in execution requests; empty in stored definitions
    std::string task_output_dir_;

    /// Artifact stem supplied at dispatch from the unit label and task ID
    std::string task_name_;

    /// Construct an empty task
    Task() = default;

    /// Construct an operation for a unit, including controller-generated work
    Task(SimulationUnitId unit_id, std::string operation,
         std::vector<caf::config_value> args = {})
        : operation_(std::move(operation)), unit_id_(unit_id), args_(std::move(args)) {}

    /// Convert positional operation arguments into config values
    template <class... Args>
    Task(SimulationUnitId unit_id, std::string operation, Args... args)
        : Task(unit_id, std::move(operation)) {
        args_.reserve(sizeof...(Args));
        const auto append_argument = [this](auto value) {
            if constexpr (std::same_as<decltype(value), TaskOutputReference>) {
                output_references_.emplace(args_.size(), value.task_id_);
                args_.emplace_back();
            } else {
                args_.emplace_back(std::move(value));
            }
        };
        (append_argument(std::move(args)), ...);
    }

    /// Construct an operation with explicit success prerequisites
    template <class... Args>
    Task(SimulationUnitId unit_id, std::vector<TaskId> dependencies, std::string operation,
         Args... args)
        : Task(unit_id, std::move(operation), std::move(args)...) {
        dependencies_ = std::move(dependencies);
    }

    /// Construct an operation with one success prerequisite
    template <class... Args>
    Task(SimulationUnitId unit_id, TaskId dependency, std::string operation, Args... args)
        : Task(unit_id, std::vector<TaskId>{dependency}, std::move(operation),
               std::move(args)...) {}

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
 * @param task_handler Callable that executes one received `Task` and returns a
 * `TaskResult` or `caf::result<TaskResult>`.
 * @return Zero after orderly worker shutdown or one after setup failure.
 */
int run_subprocess_worker(int argc, char* argv[],
                          std::function<caf::result<TaskResult>(const Task&)> task_handler);

/**
 * @brief Monotonic event offsets from job start, in nanoseconds.
 */
struct TaskTiming {
    /// Dispatch offset; -1 if the task has not dispatched
    std::int64_t dispatch_offset_ns_{-1};

    /// First terminal result offset; -1 if the task has not finished
    std::int64_t completion_offset_ns_{-1};
};

/**
 * @brief Select when a controller receives task completions and unit failures.
 */
enum class TaskGenerationPolicy {
    /// Notify after an operation completes or a unit becomes unavailable
    asynchronous,

    /// Notify when no queued or active operations remain in the current batch
    batch_synchronous,
};

/**
 * @brief Loss of one simulation unit's execution context.
 */
struct UnitFailure {
    /// Unit whose executor or process can no longer accept operations
    SimulationUnitId unit_id_{0};
    /// Diagnostic describing the loss of execution context
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
     * @param job Job whose work and unit lifetimes the controller manages.
     * @param completed_task_ids New completion IDs in completion order, delivered once per
     * task.
     * @param failed_units Units whose execution context is unavailable.
     *
     * Spans are borrowed for this call and must not be retained. Asynchronous callbacks
     * receive an operation completion or the tasks affected by unit loss. Batch callbacks
     * receive all completions and unit failures from the current batch.
     * The default policy fails the job on unit loss and permits reported task failures.
     * Unit loss also fails accepted tasks targeting that unit. Batch policy waits for
     * the current batch before reporting unit failures. Units are never rebuilt implicitly.
     * Look up definitions and results through Job::task() and Job::result().
     * Synthetic job-timeout results remain only in the final JobRecord.
     */
    virtual void callback(Job& job, std::span<const TaskId> completed_task_ids,
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
 * @brief Release each simulation unit after its single task completes.
 *
 * Use for jobs with one task per unit. Successful and failed task completions both
 * remove their units; loss of a unit's execution context fails the job.
 */
class SingleTaskPerUnitController : public JobController {
  public:
    using JobController::JobController;

    /// Remove completed tasks' units, or fail the job if a unit became unavailable
    void callback(Job& job, std::span<const TaskId> completed_task_ids,
                  std::span<const UnitFailure> failed_units) override;
};

/**
 * @brief Runtime `JobRecord` returned for one submitted job.
 */
struct JobRecord {
    /// Construct an empty job record
    JobRecord() = default;

    /**
     * @brief Construct a final `JobRecord` from indexed results, timings, and runtime.
     * @param run_id Runtime identifier for this `Runtime` instance.
     * @param job_id `JobId` assigned to this submitted job.
     * @param task_results Terminal results indexed by task ID.
     * @param task_timings Event offsets indexed by task ID.
     * @param run_time Job duration through controller finalization, excluding export.
     * @param output Structured output returned by the job controller.
     */
    JobRecord(std::uint64_t run_id, JobId job_id,
              std::vector<std::optional<TaskResult>> task_results = {},
              std::vector<TaskTiming> task_timings = {},
              std::chrono::duration<double> run_time = {}, caf::config_value output = {});

    /// Runtime identifier for this `Runtime` instance
    std::uint64_t run_id_{0};

    /// `JobId` assigned to this submitted job
    JobId job_id_{0};

    /// Terminal results indexed by task ID; every accepted task has a result
    std::vector<std::optional<TaskResult>> task_results_;

    /// Event offsets indexed by task ID
    std::vector<TaskTiming> task_timings_;

    /// Job duration through controller finalization, excluding report export
    std::chrono::duration<double> run_time_{};

    /// Structured output returned by JobController::finalize(); null by default
    caf::config_value output_;

    /// Git description captured at build time, or the configured version without Git
    std::string miaow_version_;
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
 * result, a CAF error, or a delegated response. Operation failures use a failed
 * `TaskResult`; CAF errors describe request or protocol failures. The class is intended to
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
     * Each activated simulation unit constructs its own `TaskExecutorType` instance.
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
     * @return `TaskExecutorId` assigned to this subprocess task executor.
     * @throws std::invalid_argument if `subprocess_executable` is empty.
     */
    TaskExecutorId add_subprocess(std::string subprocess_executable);

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
    /// Maximum occupied task slots per job, including timed-out native calls still running
    std::size_t max_workers() const {
        return max_workers_;
    }

    /// Set the positive limit on occupied task slots per job
    /// @throws std::invalid_argument if workers is zero.
    void set_max_workers(std::size_t workers) {
        if (workers == 0) {
            throw std::invalid_argument("max_workers must be positive");
        }
        max_workers_ = workers;
    }

    /// Timeout for one task response; expiry makes the unit unavailable. Subprocess tasks
    /// are terminated; in-process calls retain capacity until execute() returns.
    std::chrono::milliseconds task_request_timeout_;

    /// Job timeout; expiry marks unfinished tasks as failed and requests unit shutdown.
    /// Delivery waits for cleanup; in-process calls are not interrupted.
    std::chrono::milliseconds job_request_timeout_;

    /// Root output directory for run and job artifact folders; an empty value disables
    /// all output directories and files.
    std::string output_root_;

    /// Time allowed for subprocess shutdown before forced termination; non-positive
    /// values force immediate termination. Includes the active call and child cleanup.
    std::chrono::milliseconds subprocess_shutdown_grace_period_;

    /**
     * @brief Construct runtime options.
     * @param workers Positive maximum active task requests for each job.
     * @param task_timeout Task-response timeout; terminates only subprocess tasks.
     * @param job_timeout Timeout that fails unfinished tasks and requests unit shutdown.
     * @param output_root Root directory for runtime-managed output folders.
     *                   Empty string (default) disables all file output.
     * @param subprocess_shutdown_grace_period Time allowed for orderly child shutdown.
     * @throws std::invalid_argument if workers is zero.
     */
    RuntimeOptions(std::size_t workers = 1,
                   std::chrono::milliseconds task_timeout = std::chrono::minutes(1),
                   std::chrono::milliseconds job_timeout = std::chrono::minutes(5),
                   std::string output_root = "",
                   std::chrono::milliseconds subprocess_shutdown_grace_period =
                       std::chrono::seconds(5))
        : task_request_timeout_(task_timeout),
          job_request_timeout_(job_timeout), output_root_(std::move(output_root)),
          subprocess_shutdown_grace_period_(subprocess_shutdown_grace_period) {
        set_max_workers(workers);
    }

  private:
    std::size_t max_workers_{1};
};

/**
 * @brief Handle for a submitted asynchronous job.
 *
 * Call `get()` to wait for completion and retrieve the final result. It throws
 * `std::runtime_error` when the owning job actor reports a job-level failure.
 */
using JobHandle = std::future<JobRecord>;

/**
 * @brief CAF-managed simulation-unit state with an executor retained across tasks.
 *
 * Each SimulationUnit owns its own executor instance, constructed when the actor starts
 * and retained across tasks.
 */
class SimulationUnit {
  public:
    ~SimulationUnit();
    SimulationUnit(SimulationUnit&&) noexcept;
    SimulationUnit& operator=(SimulationUnit&&) noexcept;
    SimulationUnit(const SimulationUnit&) = delete;
    SimulationUnit& operator=(const SimulationUnit&) = delete;

    /// @cond
    // CAF constructs unit state on first dispatch with its execution context
    SimulationUnit(caf::event_based_actor* self, TaskExecutorId executor_id,
                   std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
                   caf::actor subprocess_registration_actor,
                   std::uint16_t subprocess_registration_port,
                   std::chrono::milliseconds task_request_timeout,
                   std::chrono::milliseconds subprocess_shutdown_grace_period);
    caf::behavior make_behavior();
    /// @endcond

  private:
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;
    std::unique_ptr<core::SubprocessState> subprocess_state_;
    TaskExecutorId task_executor_id_;
    std::unique_ptr<TaskExecutor> task_executor_;
};

/**
 * @brief A move-only job configured before submission and managed by CAF during execution.
 *
 * The job owns its simulation units, initial tasks, and controller. Task and unit IDs
 * remain valid across moves and additions. Callers must use IDs with their owning job.
 */
class Job {
  public:
    Job();
    ~Job();
    Job(Job&&);
    Job& operator=(Job&&);
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    /// Prepare a unit under a feature label unique among units accepting new work
    /// @throws std::invalid_argument if the label is empty, contains path separators,
    /// or duplicates a label of a unit accepting work.
    SimulationUnitId add_unit(std::string feature_id, TaskExecutorId executor_id);

    /// Request removal during a controller hook; accepted tasks finish before shutdown
    /// New tasks are rejected immediately; repeated removal is harmless
    void remove_unit(SimulationUnitId unit_id);

    /// Append a task before submission or during a controller hook
    /// Prerequisites must already be added to this job but need not have finished.
    /// Dispatch waits until all prerequisites succeed.
    /// Ready tasks may overtake blocked tasks on the same unit; ordering requires
    /// explicit dependencies. Each unit executes at most one task at a time.
    /// @throws std::invalid_argument if a dependency is not an earlier task in this job.
    TaskId add_task(Task task);

    /// Borrow a task definition; additions may invalidate this reference
    const Task& task(TaskId task_id) const;

    /// Borrow an indexed result; empty for unfinished tasks
    /// Additions may invalidate this reference; do not retain it across hooks
    const std::optional<TaskResult>& result(TaskId task_id) const;

    /// Return event offsets by value, including -1 for events that have not occurred
    TaskTiming timing(TaskId task_id) const;

    /// Borrow a unit label, including after removal; unit additions may invalidate it
    const std::string& unit_label(SimulationUnitId unit_id) const;

    /// Append an operation using the Task argument constructors
    /// Prerequisites follow the same submission and success rules as add_task(Task).
    template <class... Args> TaskId add_task(SimulationUnitId unit_id, Args&&... args) {
        return add_task(Task{unit_id, std::forward<Args>(args)...});
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
    void shutdown_units();
    void shutdown_unit(std::size_t unit_index);
    void prepare_tasks(std::size_t first_task_index);
    void complete_job();
    void finish_job();
    void begin_job_error_shutdown(std::string error_message);
    void advance();
    void fail_unit(std::size_t unit_index, std::string reason);
    void unit_terminated(std::size_t unit_index, const caf::error& reason);
    void record_task(std::size_t task_index, TaskResult task_result);
    bool record_dependency_failure(std::size_t task_index);
    void complete_task(std::size_t unit_index, std::size_t task_index,
                       TaskResult task_result);
    void dispatch_tasks();
    void write_job_record(const JobRecord& record) const;

    caf::event_based_actor* self_{nullptr};

    // Runtime identifier copied into the final job record
    std::uint64_t run_id_{0};

    // Runtime-assigned submission identifier copied into the final job record
    JobId job_id_{0};

    // Runtime options for this job, copied from the runtime config
    RuntimeOptions runtime_options_;

    // Controller configured before submission and retained for this job
    // The task generation policy determines when the job actor calls callback()
    // The controller appends tasks through this job and stays in the parent process
    std::unique_ptr<JobController> job_controller_{std::make_unique<JobController>()};

    // Registry used to activate units on first task demand
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;

    // Runtime-local registrar shared by the subprocess-backed units in this job
    caf::actor subprocess_registration_actor_;

    // Loopback port used by child actor systems to reach the registration actor
    std::uint16_t subprocess_registration_port_{0};

    enum class UnitState { accepting_tasks, removal_requested, stopping, failed };

    struct UnitBinding {
        std::string feature_id_;
        TaskExecutorId task_executor_id_;
        caf::actor simulation_unit_actor_;
        std::optional<std::size_t> active_task_index_;
        UnitState state_{UnitState::accepting_tasks};
    };

    // Bindings retain identity after execution resources are released
    std::vector<UnitBinding> units_;

    // Shared artifact directory, resolved once when preparing the first tasks
    std::string task_output_dir_;

    // Initial and controller-generated task payloads owned by this job actor
    // Task IDs are the index of the task in this vector
    std::vector<Task> tasks_;

    // Terminal results indexed by task ID; empty entries represent unfinished tasks
    std::vector<std::optional<TaskResult>> task_results_;

    // Completion IDs awaiting the next eligible controller callback
    std::vector<TaskId> unprocessed_finished_task_ids_;

    // Submission order prioritizes ready tasks; blocked tasks and busy targets are skipped
    std::deque<std::size_t> pending_task_indices_;

    // Occupied task slots; timed-out native calls hold their slot until actor termination
    std::size_t active_task_count_{0};

    std::vector<UnitFailure> pending_unit_failures_;

    // Failed results may invalidate dependencies of queued tasks
    bool needs_dependency_failure_check_{false};

    // Final delivery waits until every activated unit has terminated
    bool finishing_{false};
    std::optional<std::string> pending_error_message_;

    // Response promise used to deliver the final JobRecord or a job-level error
    std::optional<caf::typed_response_promise<JobRecord>> response_promise_;

    // Delayed message that tells the job actor when its configured timeout expires
    caf::disposable job_timeout_;

    // Wall-clock start time for the full job
    std::chrono::steady_clock::time_point job_start_time_;

    // Dispatch and first terminal result offsets indexed by task ID
    std::vector<TaskTiming> task_timings_;

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
        inspector.field("operation", task.operation_).fallback(std::string{}),
        inspector.field("unit_id", task.unit_id_), inspector.field("args", task.args_),
        inspector.field("task_output_dir", task.task_output_dir_),
        inspector.field("task_name", task.task_name_));
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
 * @brief Serialize or deserialize task timing offsets for CAF actor messaging.
 * @tparam Inspector CAF inspector type.
 * @param inspector Inspector driving serialization or deserialization.
 * @param timing Event offsets to inspect.
 * @return true when all fields were inspected successfully.
 */
template <class Inspector> bool inspect(Inspector& inspector, TaskTiming& timing) {
    return inspector.object(timing).fields(inspector.field("dispatch_offset_ns",
                                                           timing.dispatch_offset_ns_),
                                           inspector.field("completion_offset_ns",
                                                           timing.completion_offset_ns_));
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
            inspector.field("miaow_version", job_record.miaow_version_)
                .fallback(std::string{}),
            inspector.field("task_results", job_record.task_results_),
            inspector.field("task_timings", job_record.task_timings_),
            inspector.field("run_time", job_record.run_time_),
            inspector.field("output", job_record.output_).fallback(caf::config_value{}));
}

/**
 * @brief Actor runtime for orchestrating simulation `Task`s.
 *
 * The runtime is model-agnostic and thread-safe.
 * Each job manages its simulation units. Each SimulationUnit owns its executor instance
 * and any subprocess resources. The controller handles unavailable units; its default
 * policy fails the job.
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
     * @brief Submit a configured job for asynchronous unit execution.
     *
     * The controller's initialize(Job&) hook runs once before dispatch and may add
     * work alongside caller-provided tasks. Controller errors
     * propagate through JobHandle::get().
     * @param job Job whose units, tasks, and controller are transferred to the runtime.
     * @return Handle for retrieving the completed job record.
     * @throws std::invalid_argument if the controller is null.
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
    caf::actor orchestrator_actor_;
};

} // namespace miaow

// Register the public types carried by MIAOW actor messages
CAF_BEGIN_TYPE_ID_BLOCK(miaow_module, caf::first_custom_type_id)
CAF_ADD_TYPE_ID(miaow_module, (miaow::Task))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskResult))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskTiming))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::TaskTiming>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<std::optional<miaow::TaskResult>>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::Task>))
CAF_ADD_TYPE_ID(miaow_module, (miaow::JobRecord))
CAF_END_TYPE_ID_BLOCK(miaow_module)
