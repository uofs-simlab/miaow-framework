#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "io/metadata.hpp"

namespace miaow::core {

namespace {

void mark_job_result_failed(JobResult& job_result, const std::string& message) {
    const std::string metadata_error_message = "Metadata error: " + message;
    for (TaskResult& task_result : job_result.task_results_) {
        task_result.status_ = TaskStatus::failed;
        if (task_result.message_.empty()) {
            task_result.message_ = metadata_error_message;
            continue;
        }
        task_result.message_ += " | " + metadata_error_message;
    }
}

TaskResult make_failed_task_result(TaskId task_id,
                                   SimulationUnitId simulation_unit_id,
                                   const std::string& message) {
    TaskResult task_result;
    task_result.task_id_ = task_id;
    task_result.simulation_unit_id_ = simulation_unit_id;
    task_result.status_ = TaskStatus::failed;
    task_result.message_ = message;
    return task_result;
}

void canonicalize_task_result(TaskResult& task_result,
                              const Task& task,
                              TaskId task_id,
                              double run_time_s) {
    task_result.task_id_ = task_id;
    task_result.simulation_unit_id_ = task.simulation_unit_id_;
    task_result.run_time_s_ = run_time_s;
}

} // namespace

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state);

// Complete only when all tasks have been dispatched and finished
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
    state->diagnostics_.task_times_s_ = state->task_times_s_;
    state->diagnostics_.total_time_s_ = std::chrono::duration<double>(
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
                          TaskResult task_result) {
    // Task is done, so decrement active_task_count
    if (state->active_task_count_ > 0) {
        --state->active_task_count_;
    }

    // Worker slot is free again, so add it back to the available_workers queue
    state->available_workers_.push(worker_id);

    // Record the wall-clock duration and success/failure message for this task ID
    const double task_run_time_s = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() -
                                       state->task_start_times_.at(task_id))
                                       .count();
    state->task_times_s_.at(task_id) = task_run_time_s;

    // Runtime owns canonical identifiers and timing for task results
    canonicalize_task_result(task_result,
                             state->tasks_.at(task_id),
                             static_cast<TaskId>(task_id),
                             task_run_time_s);
    state->task_results_.at(task_id) = std::move(task_result);

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

        // Request one task execution from one simulation_unit actor
        self->mail(caf::get_atom_v, state->tasks_.at(task_id))
            .request(state->workers_.at(worker_id), state->runtime_options_.task_request_timeout_)
            .then(
                [self, state, worker_id, task_id](TaskResult task_result) {
                    complete_task(self, state, worker_id, task_id, std::move(task_result));
                },
                [self, state, worker_id, task_id](const caf::error& err) {
                    const TaskResult failed_task_result = make_failed_task_result(
                        static_cast<TaskId>(task_id),
                        state->tasks_.at(task_id).simulation_unit_id_,
                        std::string("CAF error: ") + caf::to_string(err));
                    complete_task(self,
                                  state,
                                  worker_id,
                                  task_id,
                                  failed_task_result);
                });
    }

    // Are all jobs finished? if so, deliver final results and stop the job actor
    complete_job_if_all_tasks_finished(self, state);
}

// Leaf simulation_unit actors used as workers by each job actor
caf::behavior simulation_unit_actor(caf::event_based_actor* /*self*/,
                                    std::shared_ptr<const SimulationUnitRegistry>
                                        simulation_unit_registry) {
    return {
        [simulation_unit_registry = std::move(simulation_unit_registry)](
            caf::get_atom,
            const Task& task) -> caf::result<TaskResult> {
            // Error if simulation unit registry is missing
            if (!simulation_unit_registry) {
                return caf::make_error(caf::sec::runtime_error,
                                       "SimulationUnitRegistry is null");
            }

            // Resolve the simulation unit executor for this task's simulation_unit_id
            const SimulationUnitExecutor* simulation_unit_executor =
                simulation_unit_registry->find_executor(task.simulation_unit_id_);
            if (!simulation_unit_executor) {
                return caf::make_error(
                    caf::sec::runtime_error,
                    "unknown SimulationUnitId: " + std::to_string(task.simulation_unit_id_));
            }

            // If valid simulation unit, execute its behaviour and return the result
            try {
                return (*simulation_unit_executor)(task);
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
                        std::shared_ptr<const SimulationUnitRegistry>
                            simulation_unit_registry) {
    // JobState holds all mutable state variables for the job actor
    const std::shared_ptr<JobState> state = std::make_shared<JobState>();
    state->runtime_options_ = std::move(runtime_options);
    state->tasks_ = std::move(tasks);
    state->job_start_time_ = std::chrono::steady_clock::now();

    // Initialize one default result entry per task
    state->task_results_.resize(state->tasks_.size());
    state->task_start_times_.assign(state->tasks_.size(), state->job_start_time_);
    state->task_times_s_.assign(state->tasks_.size(), 0.0);

    // If max_workers > number of tasks, set worker_count to the number of tasks
    const std::size_t worker_count =
        std::min<std::size_t>(state->runtime_options_.max_workers_, state->tasks_.size());

    // Spawn worker actors and add their IDs to the available_workers queue
    state->workers_.reserve(worker_count);
    for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
        // Each worker receives the same simulation_unit registry
        // Insert a new simulation_unit actor and track its index as worker_id
        state->workers_.emplace_back(
            self->spawn(simulation_unit_actor, simulation_unit_registry));
        state->available_workers_.push(worker_id);
    }

    return {
        // Start dispatching tasks when orchestrator requests this job
        [=](caf::get_atom) -> caf::result<JobResult> {
            if (state->response_promise_.has_value()) {
                return caf::make_error(caf::sec::runtime_error,
                                       "job_actor only accepts one get_atom request");
            }

            // Keep handler non-blocking while workers execute
            state->response_promise_ = self->make_response_promise<JobResult>();

            dispatch_tasks(self, state);
            return *state->response_promise_;
        }
    };
}

// Shared orchestrator actor that handles job submission and completion delivery
caf::behavior orchestrator_actor(caf::event_based_actor* self,
                                 RuntimeOptions runtime_options,
                                 std::uint64_t run_id,
                                 std::shared_ptr<const SimulationUnitRegistry>
                                     simulation_unit_registry,
                                 std::mutex* pending_jobs_mutex,
                                 PendingJobs* pending_jobs) {
    return {
        // Job submission event
        [=](caf::get_atom, JobId job_id, std::vector<Task> tasks) {
            const std::size_t task_count = tasks.size();
            std::vector<SimulationUnitId> task_simulation_unit_ids;
            task_simulation_unit_ids.reserve(task_count);
            for (const Task& task : tasks) {
                task_simulation_unit_ids.push_back(task.simulation_unit_id_);
            }
            const caf::actor orchestrator = self;

            // Spawn one job actor for this request
            caf::actor job = self->spawn(job_actor,
                                         std::move(tasks),
                                         runtime_options,
                                         simulation_unit_registry);

            // Forward a request to the per-job actor and wait for one JobResult reply
            self->mail(caf::get_atom_v).request(job, runtime_options.job_request_timeout_)
                .then(
                    // Forward successful completion back into this actor
                    [orchestrator, job_id](const JobResult& job_result) {
                        caf::anon_mail(caf::put_atom_v, job_id, job_result)
                            .send(orchestrator);
                    },
                    // Convert timeout or error into a failed JobResult
                    [orchestrator,
                     job_id,
                     task_count,
                     task_simulation_unit_ids](const caf::error& err) {
                        JobResult job_result;
                        job_result.task_results_.resize(task_count);
                        for (std::size_t task_index = 0; task_index < task_count; ++task_index) {
                            TaskResult task_result;
                            task_result.task_id_ = static_cast<TaskId>(task_index);
                            task_result.simulation_unit_id_ =
                                task_simulation_unit_ids.at(task_index);
                            task_result.status_ = TaskStatus::failed;
                            task_result.message_ = std::string("CAF error: ") + caf::to_string(err);
                            job_result.task_results_.at(task_index) = std::move(task_result);
                        }
                        job_result.diagnostics_.task_times_s_.assign(task_count, 0.0);

                        caf::anon_mail(caf::put_atom_v, job_id, std::move(job_result))
                            .send(orchestrator);
                    });
        },
        // Resolve Runtime promise for this completed job and mark its future ready
        [pending_jobs_mutex,
         pending_jobs,
         runtime_options,
         run_id](caf::put_atom,
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

            try {
                io::write_job_metadata(run_id, job_id, runtime_options, job_result);
            } catch (const std::exception& err) {
                mark_job_result_failed(job_result, err.what());
            } catch (...) {
                mark_job_result_failed(job_result, "unknown metadata write failure");
            }
            promise.set_value(std::move(job_result));
        }
    };
}

} // namespace miaow::core
