#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "miaow_internal.hpp"

namespace miaow::internal {

// Return failure message for a task in the format "Task {task_id} failed: {reason}"
static std::string failure_message(std::size_t task_id, const std::string& reason) {
    return "Task " + std::to_string(task_id) + " failed: " + reason;
}

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state);

// Complete only when all tasks have been dispatched and finished.
static void complete_job_if_all_tasks_finished(
    caf::event_based_actor* self,
    const std::shared_ptr<JobState>& state) {
    // No caller is waiting for a final response yet
    if (!state->response_promise_.has_value()) {
        return;
    }

    // There is still undispatched work or at least one running task
    if (state->next_task_to_dispatch_ < state->tasks_.size() ||
        state->active_task_count_ != 0) {
        return;
    }

    // All tasks are complete: deliver final results, clear the promise, and stop actor
    state->diagnostics_.task_times_s = state->task_times_s_;
    state->diagnostics_.total_time_s = std::chrono::duration<double>(
                                         std::chrono::steady_clock::now() - state->job_start_time_)
                                         .count();
    state->response_promise_->deliver(JobResult{state->task_results_, state->diagnostics_});
    state->response_promise_.reset();
    self->quit();
}

static void complete_task(caf::event_based_actor* self,
                          const std::shared_ptr<JobState>& state,
                          std::size_t worker_id,
                          std::size_t task_id,
                          bool success,
                          const std::string& message) {
    // Task is done, so decrement active_task_count
    if (state->active_task_count_ > 0) {
        --state->active_task_count_;
    }

    // Worker slot is free again, so add it back to the available_workers queue
    state->available_workers_.push(worker_id);

    // Record the wall-clock duration and success/failure message for this task ID
    state->task_times_s_.at(task_id) = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          state->task_start_times_.at(task_id))
                                          .count();

    // Record the success or failure message for this task ID in the task_results vector
    if (success) {
        state->task_results_.at(task_id) = message;
    } else {
        state->task_results_.at(task_id) = failure_message(task_id, message);
    }

    // Dispatch more tasks if there are any left, now that a worker slot is free
    dispatch_tasks(self, state);
}

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state) {
    // Dispatch tasks to available simulation_unit actors until we run out of either
    while (state->next_task_to_dispatch_ < state->tasks_.size() &&
           !state->available_workers_.empty()) {
        // Update next task to dispatch
        const std::size_t task_id = state->next_task_to_dispatch_;
        ++state->next_task_to_dispatch_;

        // Get an available worker slot and mark it as busy by popping from the queue
        const std::size_t worker_id = state->available_workers_.front();
        state->available_workers_.pop();

        // Task is active now that it's dispatched, so increment active_task_count
        ++state->active_task_count_;

        // Record dispatch time for per-task runtime diagnostics
        state->task_start_times_.at(task_id) = std::chrono::steady_clock::now();

        // Request one task execution from one simulation_unit actor.
        self->mail(caf::get_atom_v, state->tasks_.at(task_id))
            .request(state->workers_.at(worker_id), state->runtime_options_.task_request_timeout_)
            .then(
                [self, state, worker_id, task_id](const std::string& message) {
                    complete_task(self, state, worker_id, task_id, true, message);
                },
                [self, state, worker_id, task_id](const caf::error& err) {
                    complete_task(self,
                                  state,
                                  worker_id,
                                  task_id,
                                  false,
                                  std::string("CAF error: ") + caf::to_string(err));
                });
    }

    // Are all jobs finished? if so, deliver final results and stop the job actor
    complete_job_if_all_tasks_finished(self, state);
}

// Leaf simulation_unit actors used as workers by each job actor.
caf::behavior simulation_unit_actor(caf::event_based_actor* /*self*/,
                                    SimulationUnitExecutor simulation_unit_executor) {
    return {
        [simulation_unit_executor = std::move(simulation_unit_executor)](
            caf::get_atom,
            const Task& task) -> caf::result<std::string> {
            // Error if the simulation_unit_executor is not set for some reason
            if (!simulation_unit_executor) {
                return caf::make_error(caf::sec::runtime_error, "SimulationUnitExecutor is null");
            }

            // If valid simulation unit, execute its behaviour and return the result
            try {
                return simulation_unit_executor(task);
            } catch (const std::exception& exception) {
                return caf::make_error(caf::sec::runtime_error, exception.what());
            } catch (...) {
                return caf::make_error(caf::sec::runtime_error,
                                       "unknown SimulationUnitExecutor exception");
            }
        }
    };
}

// Per-job supervisor that owns simulation_unit actors
caf::behavior job_actor(caf::event_based_actor* self,
                        std::vector<Task> tasks,
                        RuntimeOptions runtime_options,
                        SimulationUnitExecutor simulation_unit_executor) {
    // JobState holds all mutable state variables for the job actor
    auto state = std::make_shared<JobState>();
    state->runtime_options_ = std::move(runtime_options);
    state->tasks_ = std::move(tasks);
    state->job_start_time_ = std::chrono::steady_clock::now();

    // Initialize results vector with empty strings for each task
    state->task_results_.assign(state->tasks_.size(), "");
    state->task_start_times_.assign(state->tasks_.size(), state->job_start_time_);
    state->task_times_s_.assign(state->tasks_.size(), 0.0);

    // If max_workers > number of tasks, set worker_count to the number of tasks
    const std::size_t worker_count =
        std::min<std::size_t>(state->runtime_options_.max_workers_, state->tasks_.size());

    // Spawn worker actors and add their IDs to the available_workers queue
    state->workers_.reserve(worker_count);
    for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
        // Each worker receives the same simulation_unit executor behavior
        // Insert a new simulation_unit actor and track its index as worker_id
        state->workers_.emplace_back(
            self->spawn(simulation_unit_actor, simulation_unit_executor));
        state->available_workers_.push(worker_id);
    }

    return {
        // Start dispatching tasks when orchestrator requests this job.
        [=](caf::get_atom) -> caf::result<JobResult> {
            if (state->response_promise_.has_value()) {
                return caf::make_error(caf::sec::runtime_error,
                                       "job_actor only accepts one get_atom request");
            }

            // Keep handler non-blocking while workers execute.
            state->response_promise_ = self->make_response_promise<JobResult>();

            dispatch_tasks(self, state);
            return *state->response_promise_;
        }
    };
}

// Shared orchestrator actor that handles job submission and completion delivery.
caf::behavior orchestrator_actor(caf::event_based_actor* self,
                                 RuntimeOptions runtime_options,
                                 SimulationUnitExecutor simulation_unit_executor,
                                 std::mutex* pending_jobs_mutex,
                                 PendingJobs* pending_jobs) {
    return {
        // Job submission event
        [=](caf::get_atom, JobId job_id, std::vector<Task> tasks) {
            const std::size_t task_count = tasks.size();
            const caf::actor orchestrator = self;

            // Spawn one job actor for this request
            caf::actor job = self->spawn(job_actor,
                                         std::move(tasks),
                                         runtime_options,
                                         simulation_unit_executor);

            // Forward a request to the per-job actor and wait for one JobResult reply.
            self->mail(caf::get_atom_v).request(job, runtime_options.job_request_timeout_)
                .then(
                    // Forward successful completion back into this actor.
                    [orchestrator, job_id](const JobResult& job_result) {
                        caf::anon_mail(caf::put_atom_v, job_id, job_result)
                            .send(orchestrator);
                    },
                    // Convert timeout/error into a failed JobResult and route it the same way.
                    [orchestrator, job_id, task_count](const caf::error& err) {
                        JobResult job_result;
                        job_result.task_results.assign(
                            task_count,
                            std::string("CAF error: ") + caf::to_string(err));
                        job_result.diagnostics.task_times_s.assign(task_count, 0.0);

                        caf::anon_mail(caf::put_atom_v, job_id, std::move(job_result))
                            .send(orchestrator);
                    });
        },
        // Resolve Runtime promise for this completed job and mark its future ready
        [pending_jobs_mutex,
         pending_jobs](caf::put_atom,
                       JobId job_id,
                       JobResult job_result) mutable {
            std::promise<JobResult> promise;
            {
                std::lock_guard<std::mutex> lock(*pending_jobs_mutex);
                PendingJobs::iterator pending_job_entry = pending_jobs->find(job_id);
                if (pending_job_entry == pending_jobs->end()) {
                    return;
                }
                promise = std::move(pending_job_entry->second);
                pending_jobs->erase(pending_job_entry);
            } // mutex unlocks automatically here

            promise.set_value(std::move(job_result));
        }
    };
}

} // namespace miaow::internal
