#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

#include <caf/all.hpp>
#include <caf/allowed_unsafe_message_type.hpp>

#include "miaow.hpp"

// Job controllers remain in the parent process and pass only through local actor messages
// CAF therefore does not need to serialize the concrete user-defined controller type
CAF_ALLOW_UNSAFE_MESSAGE_TYPE(std::shared_ptr<miaow::JobController>)

// Register the types and atoms carried by MIAOW actor messages
CAF_BEGIN_TYPE_ID_BLOCK(miaow_module, caf::first_custom_type_id)
CAF_ADD_TYPE_ID(miaow_module, (miaow::ExecutionMode))
CAF_ADD_TYPE_ID(miaow_module, (miaow::Task))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskResult))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskRecord))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::TaskRecord>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::Task>))
CAF_ADD_TYPE_ID(miaow_module, (miaow::JobRecord))
CAF_ADD_TYPE_ID(miaow_module, (std::shared_ptr<miaow::JobController>))
CAF_ADD_ATOM(miaow_module, miaow::core, job_timeout_atom, "jobtout")
CAF_ADD_ATOM(miaow_module, miaow::core, shutdown_atom, "shutdown")
// Worker to registrar: reserve an ID and associate it with that local worker
CAF_ADD_ATOM(miaow_module, miaow::core, reserve_subprocess_launch_id_atom, "reslnchid")
// Worker to registrar: remove an ID whose child must no longer be accepted
CAF_ADD_ATOM(miaow_module, miaow::core, release_subprocess_launch_id_atom, "rellnchid")
// Child to registrar, then registrar to worker: deliver the child actor handle
CAF_ADD_ATOM(miaow_module, miaow::core, register_subprocess_worker_atom, "regsubpr")
// Worker to itself: expire a child that did not register before its startup deadline
CAF_ADD_ATOM(miaow_module, miaow::core, subprocess_startup_timeout_atom, "substart")
// Worker to itself: check whether its stopping POSIX child can be reaped
CAF_ADD_ATOM(miaow_module, miaow::core, poll_subprocess_exit_atom, "pollsub")
CAF_END_TYPE_ID_BLOCK(miaow_module)

namespace miaow::core {

// Address used for communication between parent and subprocesses on the same machine
inline constexpr const char* subprocess_registration_host = "127.0.0.1";

// Prefix used to identify failures from the subprocess execution path
inline constexpr const char* subprocess_error_prefix = "Subprocess error";

// Function pointer for the CAF behaviour hosted by the subprocess executable
using SubprocessWorker = caf::behavior (*)(caf::event_based_actor*);

// Assign runtime-owned fields to tasks at or after first_task_index
// Derive each task name from feature_id metadata or its vector index; reject path
// separators and duplicate names across both earlier and assigned tasks
// Set task_output_dir_ to the job results directory, creating that directory when output is
// enabled; clear task_output_dir_ for assigned tasks when output is disabled
// Runtime passes zero for initial tasks; job actors pass the first generated task index
// Throws std::runtime_error for invalid or duplicate names and
// std::filesystem::filesystem_error when the results directory cannot be created
void assign_task_runtime_fields(std::vector<Task>& tasks, std::size_t first_task_index,
                                const RuntimeOptions& runtime_options, std::uint64_t run_id,
                                JobId job_id);

// File paths and metadata for subprocess logs
struct SubprocessPaths {
    // Subprocess stdout log used for diagnostics
    std::filesystem::path stdout_path_;

    // Subprocess stderr log used when setup or execution fails
    std::filesystem::path stderr_path_;

    // True when both log paths share a temporary directory created by MIAOW; cleanup
    // removes the directory when subprocess logs do not need to be retained
    bool temporary_control_dir_{false};
};

// Runtime-local identifier allocated before a child can register its CAF worker actor
using SubprocessLaunchId = std::uint64_t;

// Lifecycle phase changed only by the local simulation_unit_actor that owns the PID
enum class SubprocessPhase {
    // No lifecycle operation is pending; a reusable persistent child may still exist
    idle,

    // The worker is reserving a launch ID or waiting for the child actor to register
    starting_subprocess,

    // The child task request is pending and the local worker awaits its result
    waiting_for_task_result,

    // The worker requested immediate termination and is polling until waitpid reaps the PID
    stopping_subprocess,
};

// Process, child actor, and pending request state for one local simulation_unit_actor
class SubprocessState : public std::enable_shared_from_this<SubprocessState> {
  public:
    // Store fixed worker configuration once for every asynchronous transition
    SubprocessState(caf::event_based_actor* owner,
                    caf::actor subprocess_registration_actor,
                    std::uint16_t subprocess_registration_port,
                    std::chrono::milliseconds task_request_timeout);

    // Final cleanup fallback when actor-driven termination and reaping cannot finish
    ~SubprocessState();

    // Return whether shutdown prevents this worker from accepting another task
    bool shutdown_requested() const;

    // Begin asynchronous execution of one subprocess task
    caf::result<TaskResult> execute(const Task& task);

    // Accept the child actor handle for the current launch
    void accept_registration(SubprocessLaunchId launch_id,
                             const caf::actor& subprocess_worker);

    // Fail the current launch when its child does not register before the deadline
    void expire_startup(SubprocessLaunchId launch_id);

    // Poll and finish cleanup for the current stopping child
    void poll_exit(SubprocessLaunchId launch_id);

    // Stop accepting work and release any owned subprocess
    void shutdown();

  private:
    // Respond when CAF reports that the actor running in the child has terminated
    void handle_worker_termination(SubprocessLaunchId launch_id,
                                   const caf::actor_addr& subprocess_worker_address,
                                   const caf::error& reason);

    // Deliver one task result to the waiting job actor
    void complete_task_response(TaskResult task_result);

    // Finish cleanup, then respond, relaunch retained work, or quit
    void finish_stop(std::optional<int> wait_status);

    // Convert a lifecycle error into a failed task result and begin cleanup
    void fail_task(std::string message);

    // Terminate the current child and begin non-blocking exit polling
    void begin_stop();

    // Handle a task result returned by the current child actor
    void handle_task_result(SubprocessLaunchId launch_id, TaskResult task_result);

    // Handle an error returned by the current child actor request
    void handle_task_error(SubprocessLaunchId launch_id, const caf::error& error);

    // Send the retained task to the registered child actor
    void request_task_result();

    // Launch the retained task with an ID reserved by the registration actor
    void handle_launch_id(SubprocessLaunchId launch_id);

    // Handle an error returned by the launch ID reservation request
    void handle_launch_error(const caf::error& error);

    // Reserve an ID before launching a child that can register with the parent
    void request_launch_id();

    // Owning local worker actor for promises, requests, timers, and shutdown
    caf::event_based_actor* const owner_;

    // Runtime-local registrar used for launch ID reservation and child registration
    const caf::actor subprocess_registration_actor_;

    // Loopback port passed directly to each child subprocess
    const std::uint16_t subprocess_registration_port_;

    // Maximum duration of the asynchronous task request sent to the child actor
    const std::chrono::milliseconds task_request_timeout_;

    // Mutated only through callbacks serialized by the owning simulation_unit_actor
    SubprocessPhase phase_{SubprocessPhase::idle};

    // Identifies one launch so delayed messages cannot affect a replacement child
    std::optional<SubprocessLaunchId> launch_id_;

    // Exact POSIX child owned and reaped only by this simulation_unit_actor
    std::optional<pid_t> subprocess_pid_;

    // CAF proxy for the worker actor running inside subprocess_pid_
    caf::actor subprocess_worker_;

    // Promised task result from this local worker to its job actor remains pending
    std::optional<caf::typed_response_promise<TaskResult>> response_promise_;

    // Task payload retained while the child starts or an incompatible child is replaced
    std::optional<Task> active_task_;

    // Child actor result retained until the associated child PID is reaped
    std::optional<TaskResult> result_waiting_for_stop_;

    // Executable path used to decide whether a persistent child can be reused
    std::string subprocess_path_;

    // Stdout and stderr files belonging to subprocess_pid_
    std::optional<SubprocessPaths> paths_;

    // True for failure diagnosis; false removes logs after successful child shutdown
    bool keep_logs_{false};

    // Prevents new work and makes the actor quit only after any owned PID is reaped
    bool shutdown_requested_{false};
};

// Start the child CAF system, register its worker with the parent, and wait on main
int run_subprocess_worker(int argc, char* argv[], SubprocessWorker worker_factory);

// Allocate launch IDs and forward each registered child only to its local worker actor
caf::behavior subprocess_registration_actor(caf::event_based_actor* self);

// Root actor owned by Runtime: receives jobs and resolves each pending JobHandle with
// either the completed JobRecord or an exception reported by its job actor
caf::behavior
orchestrator_actor(caf::event_based_actor* self, RuntimeOptions runtime_options,
                   std::uint64_t run_id,
                   std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
                   caf::actor subprocess_registration_actor,
                   std::uint16_t subprocess_registration_port,
                   std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs);

// Per-job supervisor: owns a JobState, spawns up to max_workers simulation_unit
// worker actors, dispatches tasks to available workers, and fulfils the job's
// response promise with a JobRecord after task completion or an error after job failure
caf::behavior
job_actor(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
          std::vector<Task> tasks, std::shared_ptr<JobController> job_controller,
          RuntimeOptions runtime_options,
          std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
          caf::actor subprocess_registration_actor,
          std::uint16_t subprocess_registration_port);

// Leaf worker actor: runs in-process tasks or owns one asynchronous POSIX child at a time
caf::behavior simulation_unit_actor(
    caf::event_based_actor* self,
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
    caf::actor subprocess_registration_actor, std::uint16_t subprocess_registration_port,
    std::chrono::milliseconds task_request_timeout);

// Mutable scheduling state for one job; only its job actor reads or writes it
struct JobState {
    // Runtime identifier copied into the final job record
    std::uint64_t run_id_{0};

    // Job identifier copied into the final job record
    JobId job_id_{0};

    // Runtime options for this job, copied from the runtime config
    RuntimeOptions runtime_options_;

    // Controller received through local CAF messages and retained for this job
    // The job actor calls generate_new_tasks() after appending each TaskRecord
    // Returned tasks are appended to tasks_; the controller never leaves the parent process
    std::shared_ptr<JobController> job_controller_;

    // Registry retained so workers can be created when the controller returns tasks to be
    // executed
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry_;

    // Runtime-local registrar shared by every subprocess-capable worker in this job
    caf::actor subprocess_registration_actor_;

    // Loopback port used by child actor systems to reach the registration actor
    std::uint16_t subprocess_registration_port_{0};

    // Initial and controller-generated task payloads owned by this job actor
    // Task IDs are the index of the task in this vector
    std::vector<Task> tasks_;

    // Successful and failed task records stored in completion order
    // Each record stores its task ID; the controller receives this complete vector
    // The records are sorted by task ID when the final JobRecord is constructed
    std::vector<TaskRecord> completed_task_records_;

    // Array of worker actors currently running, indexed by worker ID
    std::vector<caf::actor> workers_;

    // Queue of worker IDs that are available to run a task
    std::queue<std::size_t> available_workers_;

    // Task ID of the next task to dispatch into an available worker slot
    std::size_t next_task_to_dispatch_{0};

    // Number of tasks currently executing
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
};

} // namespace miaow::core
