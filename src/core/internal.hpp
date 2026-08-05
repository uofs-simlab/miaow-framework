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

#include <boost/asio/io_context.hpp>
#ifndef BOOST_PROCESS_USE_STD_FS
#define BOOST_PROCESS_USE_STD_FS
#endif
#include <boost/process/v2/process.hpp>
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
CAF_END_TYPE_ID_BLOCK(miaow_module)

namespace miaow::core {

// File descriptor through which a subprocess reports its published CAF actor port
inline constexpr int subprocess_connection_fd = 3;

// Address used for communication between parent and subprocesses on the same machine
inline constexpr const char* subprocess_connection_host = "127.0.0.1";

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
    SubprocessPaths(const Task& task, const std::string& output_name);

    // Subprocess stdout log used for diagnostics
    std::filesystem::path stdout_path_;

    // Subprocess stderr log used when setup or execution fails
    std::filesystem::path stderr_path_;

    // True when both log paths share a temporary directory created by MIAOW; cleanup
    // removes the directory when subprocess logs do not need to be retained
    bool temporary_control_dir_{false};
};

// Process and CAF connection resources retained by one simulation unit worker
struct SubprocessState {
    ~SubprocessState();

    // Timeout used for one task request to the remote subprocess actor
    std::chrono::milliseconds subprocess_task_timeout_{};

    // IO context owned by the persistent subprocess process handle
    boost::asio::io_context io_context_;

    // Process handle for the active subprocess worker
    std::optional<boost::process::v2::process> subprocess_;

    // CAF actor published by the active subprocess worker
    caf::actor remote_worker_;

    // Path used to launch the active subprocess worker
    std::string subprocess_path_;

    // Host address where the active subprocess actor is published
    std::string connection_host_;

    // TCP port where the active subprocess actor is published
    std::uint16_t connection_port_{0};

    // Diagnostic files for the active subprocess worker, when launched
    std::optional<SubprocessPaths> paths_;

    // True when diagnostic logs should be kept after shutdown
    bool keep_logs_{false};
};

// Execute one task, starting or reusing the worker subprocess as its mode requires
TaskResult execute_subprocess(caf::actor_system& actor_system,
                              SubprocessState& state, const Task& task);

// Stop the active subprocess and release its process, actor, and diagnostic handles
void stop_subprocess(SubprocessState& state);

// Run the subprocess executable with the requested simulation unit behaviour
int run_subprocess_worker(int argc, char* argv[], SubprocessWorker worker_factory);

// Report the subprocess actor port to the parent through the inherited descriptor
void write_subprocess_connection_pipe(int connection_fd, std::uint16_t port);

// Root actor owned by Runtime: receives jobs and resolves each pending JobHandle with
// either the completed JobRecord or an exception reported by its job actor
caf::behavior
orchestrator_actor(caf::event_based_actor* self, RuntimeOptions runtime_options,
                   std::uint64_t run_id,
                   std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
                   std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs);

// Per-job supervisor: owns a JobState, spawns up to max_workers simulation_unit
// worker actors, dispatches tasks to available workers, and fulfils the job's
// response promise with a JobRecord after task completion or an error after job failure
caf::behavior
job_actor(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
          std::vector<Task> tasks, std::shared_ptr<JobController> job_controller,
          RuntimeOptions runtime_options,
          std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry);

// Leaf worker actor: resolves SimulationUnitId, provides execution context, and
// replies with a TaskResult
caf::behavior simulation_unit_actor(
    caf::event_based_actor* self,
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
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

    // Initial and controller-generated task payloads owned by this job actor
    // Task IDs are the index of the task in this vector
    std::vector<Task> tasks_;

    // Successful and failed task records stored in completion order
    // Each record stores its task ID; the controller receives this complete vector
    // The records are sorted by task ID when the final JobRecord is constructed
    std::vector<TaskRecord> completed_task_records_;

    // Array of worker actors currently running, indexed by worker ID
    // Handles may refer to local or remote actors
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
