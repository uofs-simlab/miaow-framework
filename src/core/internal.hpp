#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

#include <caf/all.hpp>
#include <caf/actor_from_state.hpp>
#include <caf/allowed_unsafe_message_type.hpp>

#include "miaow.hpp"

namespace miaow::core {

// Transfer the controller through a local CAF message
struct WorkflowSubmission {
    std::unique_ptr<WorkflowController> controller_;
};

} // namespace miaow::core

// Prepared jobs stay in the parent process and pass only through local messages
CAF_ALLOW_UNSAFE_MESSAGE_TYPE(std::shared_ptr<miaow::Job>)
CAF_ALLOW_UNSAFE_MESSAGE_TYPE(std::shared_ptr<miaow::core::WorkflowSubmission>)

// Register the internal types and atoms carried by MIAOW actor messages
CAF_BEGIN_TYPE_ID_BLOCK(miaow_internal_module, caf::id_block::miaow_module::end)
CAF_ADD_TYPE_ID(miaow_internal_module, (std::shared_ptr<miaow::Job>))
// Job deadline for the receiving job actor
CAF_ADD_ATOM(miaow_internal_module, miaow::core, job_timeout_atom, "jobtout")
// Shutdown request for an orchestrator, job, or simulation unit actor
CAF_ADD_ATOM(miaow_internal_module, miaow::core, shutdown_atom, "shutdown")
// Launch ID reservation carrying the owning simulation unit actor
CAF_ADD_ATOM(miaow_internal_module, miaow::core, reserve_subprocess_launch_id_atom,
             "reslnchid")
// Launch ID release carrying the ID that can no longer register
CAF_ADD_ATOM(miaow_internal_module, miaow::core, release_subprocess_launch_id_atom,
             "rellnchid")
// Child registration carrying a launch ID and subprocess worker actor
CAF_ADD_ATOM(miaow_internal_module, miaow::core, register_subprocess_worker_atom,
             "regsubpr")
// Startup deadline carrying the launch ID whose registration may expire
CAF_ADD_ATOM(miaow_internal_module, miaow::core, subprocess_startup_timeout_atom,
             "substart")
// Exit poll carrying the launch ID of the stopping child process
CAF_ADD_ATOM(miaow_internal_module, miaow::core, poll_subprocess_exit_atom, "pollsub")
// Request one shared worker slot for the supplied job ID
CAF_ADD_ATOM(miaow_internal_module, miaow::core, request_worker_slot_atom, "reqslot")
// Grant one reserved worker slot
CAF_ADD_ATOM(miaow_internal_module, miaow::core, grant_worker_slot_atom, "grantslot")
// Return one shared worker slot for the supplied job ID
CAF_ADD_ATOM(miaow_internal_module, miaow::core, return_worker_slot_atom, "retslot")
CAF_ADD_TYPE_ID(miaow_internal_module, (std::shared_ptr<miaow::core::WorkflowSubmission>))
CAF_ADD_ATOM(miaow_internal_module, miaow::core, submit_child_atom, "subchild")
CAF_END_TYPE_ID_BLOCK(miaow_internal_module)

namespace miaow::core {

// Check if the given policy string is a valid CAF scheduler policy
inline bool is_scheduler_policy(std::string_view policy) {
    return policy == "sharing" || policy == "stealing";
}

// Address used for communication between parent and subprocesses on the same machine
inline constexpr const char* subprocess_registration_host = "127.0.0.1";

// Prefix used to identify failures from the subprocess execution path
inline constexpr const char* subprocess_error_prefix = "Subprocess error";

// Application promises for root jobs submitted through Runtime::submit_job()
using PendingJobs = std::unordered_map<JobId, std::promise<JobRecord>>;

// Application promises for root workflows submitted through Runtime::submit_workflow()
using PendingWorkflows =
    std::unordered_map<WorkflowRequestId, std::promise<WorkflowRecord>>;

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

// Lifecycle phase changed only by the local actor that owns the process identifier
enum class SubprocessPhase {
    // No lifecycle operation is pending; a reusable child may still exist
    idle,

    // The unit is reserving a launch ID or waiting for child registration
    starting_subprocess,

    // The unit is awaiting the child task result
    waiting_for_task_result,

    // The unit is awaiting orderly or forced child exit and reaping it with waitpid
    stopping_subprocess,
};

// Process, child actor, and pending request state for one local simulation_unit_actor
class SubprocessState {
  public:
    // Retain the owning actor, registration endpoint, and task and shutdown limits
    SubprocessState(caf::event_based_actor* owner, caf::actor subprocess_registration_actor,
                    std::uint16_t subprocess_registration_port,
                    std::chrono::milliseconds task_request_timeout,
                    std::chrono::milliseconds shutdown_grace_period,
                    std::string log_directory);

    // Final cleanup fallback when actor-driven termination and reaping cannot finish
    ~SubprocessState();

    // Begin asynchronous execution of one subprocess task
    caf::result<TaskResult> execute(const Task& task);

    // Accept the child actor handle for the current launch
    void accept_registration(SubprocessLaunchId launch_id,
                             const caf::actor& subprocess_worker_actor);

    // Fail the current launch when its child does not register before the deadline
    void expire_startup(SubprocessLaunchId launch_id);

    // Poll and finish cleanup for the current stopping child
    void poll_for_subprocess_exit(SubprocessLaunchId launch_id);

    // Stop accepting work and release any owned subprocess
    void shutdown();

  private:
    // Respond when CAF reports that the actor running in the child has terminated
    void handle_worker_termination(SubprocessLaunchId launch_id, const caf::error& reason);

    // Deliver one task result to the waiting job actor
    void complete_task_response(TaskResult task_result);

    // Finish cleanup, then respond or quit
    void finish_stop(std::optional<int> wait_status);

    // Convert a lifecycle error into a failed task result and begin cleanup
    void fail_task(std::string message);

    // Request orderly child exit when possible and begin non-blocking exit polling
    void begin_stop();

    // Handle a task result returned by the current child actor
    void handle_task_result(SubprocessLaunchId launch_id, TaskResult task_result);

    // Handle an error returned by the current child actor request
    void handle_task_error(SubprocessLaunchId launch_id, const caf::error& error);

    // Send the retained task to the registered child actor
    void request_task_result();

    // Start a child with an ID reserved by the registration actor
    void start_subprocess(SubprocessLaunchId launch_id);

    // Handle an error returned by the launch ID reservation request
    void handle_launch_id_request_error(const caf::error& error);

    // Reserve an ID before launching a child that can register with the parent
    void request_subprocess_launch_id();

    // Owning simulation-unit actor serializes requests, timers, and shutdown
    caf::event_based_actor* const owner_;

    // Runtime-local registrar used for launch ID reservation and child registration
    const caf::actor subprocess_registration_actor_;

    // Loopback port passed directly to each child subprocess
    const std::uint16_t subprocess_registration_port_;

    // Maximum duration of the asynchronous task request sent to the child actor
    const std::chrono::milliseconds task_request_timeout_;

    // Maximum time for the active call and child cleanup before forced termination
    const std::chrono::milliseconds shutdown_grace_period_;

    // Dedicated diagnostic directory; empty selects a temporary directory per launch
    const std::filesystem::path log_directory_;

    // Resources retained in each lifecycle phase
    // - idle: no response is pending; a reusable child may remain
    // - starting_subprocess: active request and response; child resources become
    //   available during launch
    // - waiting_for_task_result: response and all child resources; payload is forwarded
    // - stopping_subprocess: child cleanup and possibly a failed task result
    // Requests retain their Task until forwarding; the unit executable spans the lifecycle
    // Mutated only through callbacks serialized by the owning simulation_unit_actor
    SubprocessPhase phase_{SubprocessPhase::idle};

    // Child resources while idle
    // - Empty subprocess_pid_: no child is owned
    // - Process identifier, actor, and executable: a child remains reusable

    // Identifies one launch so stale messages cannot affect another unit
    std::optional<SubprocessLaunchId> launch_id_;

    // Exact POSIX child owned and reaped only by this simulation_unit_actor
    std::optional<pid_t> subprocess_pid_;

    // Fixed deadline while awaiting orderly child exit; cleared after forced termination
    std::optional<std::chrono::steady_clock::time_point> shutdown_deadline_;

    // CAF proxy for the worker actor running in the child identified by subprocess_pid_
    caf::actor subprocess_worker_actor_;

    // Deferred response to the job actor; pending() reports whether delivery remains
    caf::typed_response_promise<TaskResult> response_promise_;

    // Task payload retained until it is forwarded to the registered child
    std::optional<Task> active_task_;

    // Failure response retained until child cleanup finishes
    std::optional<TaskResult> result_waiting_for_stop_;

    // The unit retains the registry that owns this registration.
    const TaskExecutorRegistry::SubprocessRegistration* registration_{nullptr};

    friend class miaow::TaskExecutorRegistry;

    // Stdout and stderr files belonging to the child identified by subprocess_pid_
    std::optional<SubprocessPaths> paths_;

    // True for failure diagnosis; false removes logs after successful child shutdown
    bool keep_logs_{false};

    // Prevents new work and makes the actor quit only after any owned child is reaped
    bool shutdown_requested_{false};

    // Context loss is reported as actor termination, independently of task failures
    bool context_failed_{false};
};

// Allocate launch IDs and forward child handles to their owning unit actors
class SubprocessRegistration {
  public:
    caf::behavior make_behavior();

  private:
    SubprocessLaunchId next_launch_id_{0};
    std::unordered_map<SubprocessLaunchId, caf::actor> launch_owner_actors_;
};

// Root actor owned by Runtime: starts jobs and workflows and delivers their results
class Orchestrator {
  public:
    Orchestrator(caf::event_based_actor* self, RuntimeOptions runtime_options,
                 std::string run_directory,
                 std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
                 caf::actor subprocess_registration_actor,
                 std::uint16_t subprocess_registration_port,
                 std::atomic<JobId>* next_job_id, std::mutex* pending_results_mutex,
                 PendingJobs* pending_jobs, PendingWorkflows* pending_workflows);

    caf::behavior make_behavior();

  private:
    void start_job(JobId job_id, std::shared_ptr<Job> prepared_job,
                   std::function<void(caf::expected<JobRecord>)> complete);
    void finish_application_job(JobId job_id, caf::expected<JobRecord> result);
    void start_workflow(std::shared_ptr<WorkflowSubmission> submission,
                        std::optional<WorkflowRequestId> application_request_id,
                        caf::typed_response_promise<WorkflowRecord> parent_result = {});
    void finish_workflow(caf::actor_id workflow_id, caf::expected<WorkflowRecord> result);
    void grant_worker_slots();
    void forget_job(JobId job_id);

    caf::event_based_actor* self_;
    RuntimeOptions runtime_options_;
    std::string run_directory_;
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry_;
    caf::actor subprocess_registration_actor_;
    std::uint16_t subprocess_registration_port_;

    // Runtime-owned job ID counter shared by root jobs and workflow child jobs
    std::atomic<JobId>* next_job_id_;

    // Runtime owns these maps and their mutex through actor shutdown. They hold application
    // promises for root submissions; workflow children use CAF response promises.
    std::mutex* pending_results_mutex_;
    PendingJobs* pending_jobs_;
    PendingWorkflows* pending_workflows_;

    struct ActiveJob {
        caf::actor job_actor_;
        // Reservations charged to this job, including grants in transit
        std::size_t granted_worker_slots_{0};
    };
    // Active job actors at every depth, including workflow children
    std::unordered_map<JobId, ActiveJob> active_jobs_;

    struct ActiveWorkflow {
        caf::actor workflow_actor_;
        std::optional<WorkflowRequestId> application_request_id_;
        caf::typed_response_promise<WorkflowRecord> parent_result_;
    };
    // Active workflow actors at every depth, including nested workflows
    std::unordered_map<caf::actor_id, ActiveWorkflow> active_workflows_;

    // One request per job at a time. Grant slots in request order.
    std::deque<JobId> worker_slot_requests_;

    // Total reservations charged to all active jobs
    std::size_t granted_worker_slots_{0};
};

// Child-process actor hosting the public Task-to-TaskResult handler
class SubprocessWorker {
  public:
    explicit SubprocessWorker(
        std::function<caf::result<TaskResult>(const Task&)> task_handler);

    caf::behavior make_behavior();

  private:
    std::function<caf::result<TaskResult>(const Task&)> task_handler_;
};

} // namespace miaow::core
