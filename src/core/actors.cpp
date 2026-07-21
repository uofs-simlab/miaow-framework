#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "io/internal.hpp"

namespace miaow::core {

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state);

// Tell worker actors to release any process resources owned by this job
static void stop_workers(const std::shared_ptr<JobState>& state) {
    for (const caf::actor& worker : state->workers_) {
        caf::anon_mail(shutdown_atom_v).send(worker);
    }
}

// Complete only when all tasks have been dispatched and finished
static void complete_job_if_all_tasks_finished(caf::event_based_actor* self,
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

    // Keep task records in submitted task order even when workers finish out of order
    std::vector<TaskRecord> task_records = std::move(state->completed_task_records_);
    std::sort(task_records.begin(), task_records.end(),
              [](const TaskRecord& left, const TaskRecord& right) {
                  return left.task_id_ < right.task_id_;
              });

    // All tasks are complete: build the final record and stop the job actor
    const double job_run_time_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      state->job_start_time_)
            .count();
    JobRecord job_record{state->run_id_, state->job_id_, std::move(task_records),
                         job_run_time_s};

    stop_workers(state);
    state->response_promise_->deliver(std::move(job_record));
    state->response_promise_.reset();
    self->quit();
}

static void complete_task(caf::event_based_actor* self,
                          const std::shared_ptr<JobState>& state, std::size_t worker_id,
                          std::size_t task_id, TaskResult task_result) {
    // Task is done, so decrement active_task_count
    if (state->active_task_count_ > 0) {
        --state->active_task_count_;
    }

    // Worker slot is free again, so add it back to the available_workers queue
    state->available_workers_.push(worker_id);

    // Record the wall-clock duration and success/failure message for this task ID
    const double task_run_time_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      state->task_start_times_.at(task_id)).count();

    // Runtime creates a task record only after the task has completed
    state->completed_task_records_.push_back(
        TaskRecord{state->tasks_.at(task_id), static_cast<TaskId>(task_id),
                   std::move(task_result), task_run_time_s});

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

        // Subprocess execution enforces its timeout before returning to this actor
        caf::timespan simulation_unit_request_timeout = caf::infinite;
        if (state->tasks_.at(task_id).execution_mode_ == ExecutionMode::in_process) {
            simulation_unit_request_timeout =
                state->runtime_options_.task_request_timeout_;
        }

        // Request one task execution from one simulation_unit actor
        self->mail(caf::get_atom_v, state->tasks_.at(task_id))
            .request(state->workers_.at(worker_id),
                     simulation_unit_request_timeout)
            .then(
                [self, state, worker_id, task_id](TaskResult task_result) {
                    complete_task(self, state, worker_id, task_id, std::move(task_result));
                },
                [self, state, worker_id, task_id](const caf::error& err) {
                    const TaskResult failed_task_result =
                        miaow::make_failed_task_result(caf::to_string(err), "CAF error");
                    complete_task(self, state, worker_id, task_id, failed_task_result);
                });
    }

    // Are all jobs finished? if so, deliver final results and stop the job actor
    complete_job_if_all_tasks_finished(self, state);
}

// Leaf simulation_unit actors used as workers by each job actor
caf::behavior simulation_unit_actor(
    caf::event_based_actor* self,
    std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
    std::chrono::milliseconds task_request_timeout) {
    // Create a slot for the subprocess state - this will be empty for in-process execution
    std::shared_ptr<SubprocessState> active_subprocess =
        std::make_shared<SubprocessState>();
    active_subprocess->subprocess_task_timeout_ = task_request_timeout;

    return {
        // Execute one task using a fresh simulation unit instance from the shared registry
        [self, simulation_unit_registry = std::move(simulation_unit_registry),
         active_subprocess](caf::get_atom,
                            const Task& task) -> caf::result<TaskResult> {
            // Error if simulation unit registry is missing
            if (!simulation_unit_registry) {
                return caf::make_error(caf::sec::runtime_error,
                                       "SimulationUnitRegistry is null");
            }

            // Resolve the simulation unit factory for this task's SimulationUnitId
            const SimulationUnitFactory* simulation_unit_factory =
                simulation_unit_registry->find_factory(task.simulation_unit_id_);
            if (!simulation_unit_factory) {
                return caf::make_error(caf::sec::runtime_error,
                                       "unknown SimulationUnitId: " +
                                           std::to_string(task.simulation_unit_id_));
            }

            try {
                // Context gives the unit access to this worker's subprocess slot
                ExecutionContext context{self->system(), *active_subprocess};

                // Execute a fresh simulation unit instance for this task
                std::unique_ptr<SimulationUnit> simulation_unit =
                    (*simulation_unit_factory)();
                return simulation_unit->execute(task, context);
            } catch (const std::exception& exception) {
                return caf::make_error(caf::sec::runtime_error, exception.what());
            } catch (...) {
                return caf::make_error(caf::sec::runtime_error,
                                       "unknown SimulationUnit exception");
            }
        },
        // Stop this worker's subprocess when it receives a shutdown message
        [self, active_subprocess](shutdown_atom) {
            stop_subprocess(*active_subprocess);
            self->quit();
        }};
}

// Per-job supervisor that owns simulation_unit actors
caf::behavior
job_actor(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
          std::vector<Task> tasks, RuntimeOptions runtime_options,
          std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry) {
    // JobState holds all mutable state variables for the job actor
    const std::shared_ptr<JobState> state = std::make_shared<JobState>();
    state->run_id_ = run_id;
    state->job_id_ = job_id;
    state->runtime_options_ = std::move(runtime_options);
    state->tasks_ = std::move(tasks);
    state->job_start_time_ = std::chrono::steady_clock::now();

    // Completed task records are appended as workers return terminal results
    state->completed_task_records_.reserve(state->tasks_.size());

    // Task start times are filled when each task is dispatched
    state->task_start_times_.assign(state->tasks_.size(), state->job_start_time_);

    // If max_workers > number of tasks, set worker_count to the number of tasks
    const std::size_t worker_count =
        std::min<std::size_t>(state->runtime_options_.max_workers_, state->tasks_.size());

    // Workers use Task::simulation_unit_id_ to select a factory from the shared registry
    // The selected factory creates a fresh SimulationUnit for each task

    // Spawn simulation_unit actors and queue their worker indices
    state->workers_.reserve(worker_count);
    for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
        state->workers_.emplace_back(self->spawn(
            simulation_unit_actor, simulation_unit_registry,
            state->runtime_options_.task_request_timeout_));
        state->available_workers_.push(worker_id);
    }

    return {// Start dispatching tasks when orchestrator requests this job
            [=](caf::get_atom) -> caf::result<JobRecord> {
                if (state->response_promise_.has_value()) {
                    return caf::make_error(caf::sec::runtime_error,
                                           "job_actor only accepts one get_atom request");
                }

                // Keep handler non-blocking while workers execute
                state->response_promise_ = self->make_response_promise<JobRecord>();

                dispatch_tasks(self, state);
                return *state->response_promise_;
            },
            // Stop workers when the owning job actor is asked to shut down
            [=](shutdown_atom) {
                stop_workers(state);
                self->quit();
            }};
}

// Shared orchestrator actor that handles job submission and completion delivery
caf::behavior
orchestrator_actor(caf::event_based_actor* self, RuntimeOptions runtime_options,
                   std::uint64_t run_id,
                   std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
                   std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs) {
    return {// Job submission event
            [=](caf::get_atom, JobId job_id, std::vector<Task> tasks) {
                const caf::actor orchestrator = self;

                // Spawn one job actor for this request
                caf::actor job = self->spawn(job_actor, run_id, job_id, tasks,
                                             runtime_options, simulation_unit_registry);

                // Forward a request to the per-job actor and wait for one JobRecord reply
                self->mail(caf::get_atom_v)
                    .request(job, runtime_options.job_request_timeout_)
                    .then(
                        // Forward successful completion back into this actor
                        [orchestrator, job_id](const JobRecord& job_record) {
                            caf::anon_mail(caf::put_atom_v, job_id, job_record)
                                .send(orchestrator);
                        },
                        // Convert job request timeout or error into a failed JobRecord
                        [orchestrator, job, job_id, run_id,
                         tasks](const caf::error& err) {
                            caf::anon_mail(shutdown_atom_v).send(job);
                            std::vector<TaskRecord> task_records;
                            task_records.reserve(tasks.size());
                            for (std::size_t task_index = 0; task_index < tasks.size();
                                 ++task_index) {
                                TaskResult task_result =
                                    make_failed_task_result(caf::to_string(err),
                                                            "CAF error");
                                task_records.push_back(
                                    TaskRecord{tasks.at(task_index),
                                               static_cast<TaskId>(task_index),
                                               std::move(task_result), 0.0});
                            }
                            JobRecord job_record{run_id, job_id, std::move(task_records)};

                            caf::anon_mail(caf::put_atom_v, job_id, std::move(job_record))
                                .send(orchestrator);
                        });
            },
            // Resolve Runtime promise for this completed job and mark its future ready
            [pending_jobs_mutex, pending_jobs,
             runtime_options](caf::put_atom, JobId job_id, JobRecord job_record) mutable {
                std::promise<JobRecord> promise;
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
                    // Write the job record to disk if an output root is configured
                    if (!runtime_options.output_root_.empty()) {
                        const std::filesystem::path job_directory =
                            std::filesystem::path(runtime_options.output_root_) /
                            ("run_" + std::to_string(job_record.run_id_)) /
                            ("job_" + std::to_string(job_record.job_id_));
                        std::filesystem::create_directories(job_directory);
                        io::write_json_file(job_directory / "job_record.json", job_record,
                                            false);
                    }
                } catch (...) {
                    // Output record failures are reported without changing task status
                    promise.set_exception(std::current_exception());
                    return;
                }
                promise.set_value(std::move(job_record));
            }};
}

} // namespace miaow::core
