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

#include "miaow.hpp"

// Register MIAOW CAF type IDs for actor message serialization
CAF_BEGIN_TYPE_ID_BLOCK(miaow_module, caf::first_custom_type_id)
CAF_ADD_TYPE_ID(miaow_module, (miaow::ExecutionMode))
CAF_ADD_TYPE_ID(miaow_module, (miaow::Task))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskResult))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskRecord))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::TaskRecord>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::Task>))
CAF_ADD_TYPE_ID(miaow_module, (miaow::JobRecord))
CAF_ADD_ATOM(miaow_module, miaow::core, shutdown_atom, "shutdown")
CAF_END_TYPE_ID_BLOCK(miaow_module)

namespace miaow::core {

inline constexpr int subprocess_connection_fd = 3;
inline constexpr const char* subprocess_connection_host = "127.0.0.1";
inline constexpr const char* subprocess_error_prefix = "Subprocess error";

using SubprocessWorker = caf::behavior (*)(caf::event_based_actor*);

struct SubprocessPaths {
    // Subprocess stdout log used for diagnostics
    std::filesystem::path stdout_path_;

    // Subprocess stderr log used when setup or execution fails
    std::filesystem::path stderr_path_;

    // True when the control directory was created only for this task
    bool temporary_control_dir_{false};
};

struct SubprocessState {
    ~SubprocessState();

    // IO context owned by the persistent subprocess process handle
    boost::asio::io_context io_context_;

    // Process handle for the active subprocess worker
    std::optional<boost::process::v2::process> subprocess_;

    // CAF actor published by the active subprocess worker
    caf::actor remote_worker_;

    // Path used to launch the active subprocess worker
    std::string subprocess_path_;

    // Diagnostic files for the active subprocess worker, when launched
    std::optional<SubprocessPaths> paths_;

    // True when diagnostic logs should be kept after shutdown
    bool keep_logs_{false};
};

TaskResult execute_subprocess(caf::actor_system& actor_system,
                              SubprocessState& state, const Task& task);

void stop_subprocess(SubprocessState& state);

int run_subprocess_worker(int argc, char* argv[], SubprocessWorker worker_factory);

void write_subprocess_connection_pipe(int connection_fd, std::uint16_t port);

// Root actor owned by Runtime: receives job submissions, spawns one job_actor per
// JobId, and delivers each completed JobRecord back to the user-facing future
// registered in pending_jobs (guarded by pending_jobs_mutex).
caf::behavior
orchestrator_actor(caf::event_based_actor* self, RuntimeOptions runtime_options,
                   std::uint64_t run_id,
                   std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
                   std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs);

// Per-job supervisor: owns a JobState, spawns up to max_workers simulation_unit
// worker actors, dispatches tasks to available workers, and fulfils the job's
// response promise with the aggregated JobRecord once every task has completed.
caf::behavior
job_actor(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
          std::vector<Task> tasks, RuntimeOptions runtime_options,
          std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry);

// Leaf worker actor: resolves SimulationUnitId, provides execution context, and
// replies with a TaskResult
caf::behavior simulation_unit_actor(
    caf::event_based_actor* self,
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry);

// Internal scheduler state for one job_actor instance
// job_actor is the only owner/writer of this state
// Worker actors never write to this directly and only communicate by message replies
struct JobState {
    // Runtime identifier copied into the final job record
    std::uint64_t run_id_{0};

    // Job identifier copied into the final job record
    JobId job_id_{0};

    // Runtime options for this job, copied from the runtime config
    RuntimeOptions runtime_options_;

    // Task payloads, passed in the initial message to job_actor
    // Task IDs are the index of the task in this vector
    std::vector<Task> tasks_;

    // Task records are created only after task execution reaches a terminal status
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

    // Response promise for the job actor used to deliver the final record to orchestrator
    std::optional<caf::typed_response_promise<JobRecord>> response_promise_;

    // Wall-clock start time for the full job
    std::chrono::steady_clock::time_point job_start_time_;

    // Start times for each task, indexed by task ID
    std::vector<std::chrono::steady_clock::time_point> task_start_times_;
};

} // namespace miaow::core
