#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <caf/all.hpp>

#include "miaow.hpp"

// Register MIAOW CAF type IDs for actor message serialization
CAF_BEGIN_TYPE_ID_BLOCK(miaow_module, caf::first_custom_type_id)
CAF_ADD_TYPE_ID(miaow_module, (miaow::Task))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskResult))
CAF_ADD_TYPE_ID(miaow_module, (miaow::TaskRecord))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::TaskRecord>))
CAF_ADD_TYPE_ID(miaow_module, (std::vector<miaow::Task>))
CAF_ADD_TYPE_ID(miaow_module, (miaow::JobRecord))
CAF_END_TYPE_ID_BLOCK(miaow_module)

namespace miaow::core {

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

// Leaf worker actor: on each Task message, resolves the matching
// SimulationUnitExecutor from the shared registry, runs it, and replies with a
// TaskResult (or a CAF error if the executor throws or the ID is unknown).
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
