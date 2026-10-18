#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "io/internal.hpp"

namespace miaow::core {

// Forward declaration of the function that dispatches tasks to available workers
static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state);

static void begin_job_error_shutdown(caf::event_based_actor* self,
                                     const std::shared_ptr<JobState>& state,
                                     std::string error_message);

// Request shutdown from every worker and any subprocess it retains
static void stop_workers(const std::shared_ptr<JobState>& state) {
    for (const caf::actor& worker : state->workers_) {
        // job_actor -> simulation_unit_actor: stop work and release child resources
        caf::anon_mail(shutdown_atom_v).urgent().send(worker);
    }
}

// Add workers to match tasks without TaskRecords, capped by max_workers_
// Called during job setup and after controller-generated tasks are appended
static void spawn_workers_for_tasks(caf::event_based_actor* self,
                                    const std::shared_ptr<JobState>& state) {
    // Each task that has succeeded or failed has one entry in completed_task_records_
    const std::size_t task_count_without_records =
        state->tasks_.size() - state->completed_task_records_.size();
    const std::size_t worker_count =
        std::min(state->runtime_options_.max_workers_, task_count_without_records);
    while (state->workers_.size() < worker_count) {
        const std::size_t worker_id = state->workers_.size();
        state->workers_.emplace_back(
            self->spawn(simulation_unit_actor, state->simulation_unit_registry_,
                        state->subprocess_registration_actor_,
                        state->subprocess_registration_port_,
                        state->runtime_options_.task_request_timeout_));
        state->available_workers_.push(worker_id);
    }
}

// Prepare controller-generated tasks for dispatch using the normal runtime fields
static void append_controller_tasks(caf::event_based_actor* self,
                                     const std::shared_ptr<JobState>& state,
                                     std::vector<Task> tasks) {
    if (tasks.empty()) {
        return;
    }
    const std::size_t first_task_index = state->tasks_.size();
    state->tasks_.reserve(first_task_index + tasks.size());
    for (Task& task : tasks) {
        state->tasks_.push_back(std::move(task));
    }
    assign_task_runtime_fields(state->tasks_, first_task_index, state->runtime_options_,
                               state->run_id_, state->job_id_);
    state->task_start_times_.resize(state->tasks_.size(), state->job_start_time_);
    state->task_worker_ids_.resize(state->tasks_.size());
    spawn_workers_for_tasks(self, state);
}

// Deliver the records currently held by the job actor and request worker shutdown
static void complete_job(caf::event_based_actor* self,
                         const std::shared_ptr<JobState>& state) {
    // Ignore completion attempts after the job response has already been delivered
    if (!state->response_promise_.has_value()) {
        return;
    }

    // Collect controller-owned output before handing the final record to the caller.
    caf::config_value output;
    try {
        output = state->job_controller_->finalize();
    } catch (const std::exception& exception) {
        begin_job_error_shutdown(
            self, state, "JobController error: " + std::string(exception.what()));
        return;
    } catch (...) {
        begin_job_error_shutdown(self, state, "JobController error: unknown exception");
        return;
    }

    // Keep task records in submitted task order even when workers finish out of order
    std::vector<TaskRecord> task_records = std::move(state->completed_task_records_);
    std::sort(task_records.begin(), task_records.end(),
              [](const TaskRecord& left, const TaskRecord& right) {
                  return left.task_id_ < right.task_id_;
              });

    // Build the final record and stop the job actor
    const std::chrono::duration<double> job_run_time =
        std::chrono::steady_clock::now() - state->job_start_time_;
    JobRecord job_record{state->run_id_, state->job_id_, std::move(task_records),
                         job_run_time, std::move(output)};

    // Stop job-owned work before completing the deferred request from the orchestrator
    state->job_timeout_.dispose();
    stop_workers(state);

    // Fulfil the job actor's CAF response so the orchestrator receives the JobRecord
    state->response_promise_->deliver(std::move(job_record));
    state->response_promise_.reset();
    self->quit();
}

// Deliver a pending job-level error after all active requests have finished
static void complete_job_error_if_tasks_finished(caf::event_based_actor* self,
                                                 const std::shared_ptr<JobState>& state) {
    if (!state->pending_error_message_.has_value() || state->active_task_count_ != 0 ||
        !state->response_promise_.has_value()) {
        return;
    }

    // Complete the outstanding CAF request with an error for the orchestrator to forward
    state->response_promise_->deliver(
        caf::make_error(caf::sec::runtime_error,
                        std::move(*state->pending_error_message_)));
    state->response_promise_.reset();
    self->quit();
}

// Stop dispatch after a job-level error and wait for active workers to finish
static void begin_job_error_shutdown(caf::event_based_actor* self,
                                     const std::shared_ptr<JobState>& state,
                                     std::string error_message) {
    if (state->pending_error_message_.has_value()) {
        return;
    }

    state->pending_error_message_ = std::move(error_message);
    state->job_timeout_.dispose();

    // Urgent shutdown runs after active execution but before queued task requests
    stop_workers(state);
    complete_job_error_if_tasks_finished(self, state);
}

// Complete when no queued tasks remain to dispatch and no dispatched tasks await results
static void complete_job_if_all_tasks_finished(caf::event_based_actor* self,
                                               const std::shared_ptr<JobState>& state) {
    // There is still queued work or at least one dispatched task awaiting a result
    if (state->next_task_to_dispatch_ < state->tasks_.size() ||
        state->active_task_count_ != 0) {
        return;
    }

    complete_job(self, state);
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
    const std::chrono::duration<double> task_run_time =
        std::chrono::steady_clock::now() - state->task_start_times_.at(task_id);
    const std::chrono::duration<double> dispatch_offset_from_job_start =
        state->task_start_times_.at(task_id) - state->job_start_time_;

    // Runtime creates a task record only after the task has completed
    state->completed_task_records_.push_back(
        TaskRecord{state->tasks_.at(task_id), static_cast<TaskId>(task_id),
                   std::move(task_result), task_run_time, dispatch_offset_from_job_start,
                   worker_id});

    // During error shutdown, record active results without generating or dispatching work
    if (state->pending_error_message_.has_value()) {
        complete_job_error_if_tasks_finished(self, state);
        return;
    }

    // Check whether the controller uses batch-synchronous task generation
    if (state->job_controller_->task_generation_policy() ==
        TaskGenerationPolicy::batch_synchronous) {
        // Check whether queued tasks remain to dispatch or there are dispatched tasks
        // awaiting results; if so, dispatch tasks and return without generating new tasks.
        if (state->next_task_to_dispatch_ < state->tasks_.size() ||
            state->active_task_count_ != 0) {
            dispatch_tasks(self, state);
            return;
        }
        // If we reach this point, it means there are no queued tasks and no active tasks,
        // so we can proceed to generate new tasks. This will be a new batch of tasks.
    }

    try {
        // Deliver each completion once through a view of the new records
        const std::span<const TaskRecord> records{state->completed_task_records_};

        // Call the job controller to generate new tasks based on the completed task records
        std::vector<Task> generated_tasks =
            state->job_controller_->generate_new_tasks(
                records.subspan(state->reported_task_count_));

        // Update the count of reported tasks to include the newly completed ones
        state->reported_task_count_ = records.size();

        // Add the generated tasks to the queue for dispatching
        append_controller_tasks(self, state, std::move(generated_tasks));
    } catch (const std::exception& exception) {
        begin_job_error_shutdown(
            self, state, "JobController error: " + std::string(exception.what()));
        return;
    } catch (...) {
        begin_job_error_shutdown(self, state,
                                 "JobController error: unknown exception");
        return;
    }

    // Dispatch more tasks if there are any left, now that a worker slot is free
    dispatch_tasks(self, state);
}

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state) {
    // A controller error prevents pending tasks from entering worker mailboxes
    if (state->pending_error_message_.has_value()) {
        complete_job_error_if_tasks_finished(self, state);
        return;
    }

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

        // Retain the dispatch time and worker assignment for task diagnostics
        state->task_start_times_.at(task_id) = std::chrono::steady_clock::now();
        state->task_worker_ids_.at(task_id) = worker_id;

        // SubprocessState owns its timeout so cleanup precedes the delegated response
        const caf::timespan simulation_unit_request_timeout =
            SimulationUnitRegistryAccess::uses_subprocess(
                *state->simulation_unit_registry_,
                state->tasks_.at(task_id).simulation_unit_id_)
                ? caf::infinite
                : state->runtime_options_.task_request_timeout_;

        // job_actor -> simulation_unit_actor: request execution of one Task;
        // caf::get_atom_v is matched by caf::get_atom in the worker handler, request()
        // returns a response handle, and then() installs one response behaviour
        self->mail(caf::get_atom_v, state->tasks_.at(task_id))
            .request(state->workers_.at(worker_id), simulation_unit_request_timeout)
            .then(
                // simulation_unit_actor -> job_actor: return the completed task result
                [self, state, worker_id, task_id](TaskResult task_result) {
                    complete_task(self, state, worker_id, task_id, std::move(task_result));
                },
                // simulation_unit_actor -> job_actor: report a task request error
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
    caf::actor subprocess_registration_actor, std::uint16_t subprocess_registration_port,
    std::chrono::milliseconds task_request_timeout) {
    // CAF callbacks outlive this function, so they share the subprocess lifecycle state
    // Only this actor's serialized handlers and callbacks mutate the shared object
    const std::shared_ptr<SubprocessState> subprocess_state =
        std::make_shared<SubprocessState>(self,
                                          std::move(subprocess_registration_actor),
                                          subprocess_registration_port,
                                          task_request_timeout);

    return {
        // job_actor -> simulation_unit_actor: execute one Task and return or defer its
        // result; caf::get_atom matches the caf::get_atom_v value sent by job_actor, and
        // CAF interprets caf::result<TaskResult> as a value, error, or delegated response
        [simulation_unit_registry = std::move(simulation_unit_registry),
         subprocess_state](caf::get_atom, const Task& task) -> caf::result<TaskResult> {
            try {
                // Construct the registered polymorphic unit for this task
                std::unique_ptr<SimulationUnit> simulation_unit =
                    SimulationUnitRegistryAccess::construct(*simulation_unit_registry,
                                                            task.simulation_unit_id_,
                                                            *subprocess_state);
                if (!simulation_unit) {
                    return caf::make_error(caf::sec::runtime_error,
                                           "unknown SimulationUnitId: " +
                                               std::to_string(task.simulation_unit_id_));
                }
                return simulation_unit->execute(task);
            } catch (const std::exception& exception) {
                return caf::make_error(caf::sec::runtime_error, exception.what());
            } catch (...) {
                return caf::make_error(caf::sec::runtime_error,
                                       "unknown SimulationUnit exception");
            }
        },
        // subprocess_registration_actor -> simulation_unit_actor: deliver the child
        // handle; accept only the current launch, install CAF monitoring, and send the
        // task retained while the child process started
        [subprocess_state](register_subprocess_worker_atom, SubprocessLaunchId launch_id,
                           const caf::actor& subprocess_worker) {
            subprocess_state->accept_registration(launch_id, subprocess_worker);
        },
        // simulation_unit_actor -> simulation_unit_actor: enforce the registration
        // deadline; the launch ID makes this delayed message harmless after registration,
        // failure, or replacement advances the worker to another lifecycle state
        [subprocess_state](subprocess_startup_timeout_atom, SubprocessLaunchId launch_id) {
            subprocess_state->expire_startup(launch_id);
        },
        // simulation_unit_actor -> simulation_unit_actor: poll the stopping child process;
        // each callback performs one non-blocking waitpid check and reschedules itself
        // until the child is reaped and the worker can complete its next transition
        [subprocess_state](poll_subprocess_exit_atom, SubprocessLaunchId launch_id) {
            subprocess_state->poll_for_subprocess_exit(launch_id);
        },
        // job_actor -> simulation_unit_actor: stop work and release owned resources;
        // resolve any pending task response and keep this actor alive until launch
        // reservation or child process reaping completes
        [subprocess_state](shutdown_atom) { subprocess_state->shutdown(); }};
}

// Per-job supervisor that owns simulation_unit actors
caf::behavior
job_actor(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
          std::vector<Task> tasks, std::shared_ptr<JobController> job_controller,
          bool initialize_controller, RuntimeOptions runtime_options,
          std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
          caf::actor subprocess_registration_actor,
          std::uint16_t subprocess_registration_port) {
    // JobState holds all mutable state variables for the job actor
    const std::shared_ptr<JobState> state = std::make_shared<JobState>();
    state->run_id_ = run_id;
    state->job_id_ = job_id;
    state->runtime_options_ = std::move(runtime_options);
    state->job_controller_ = std::move(job_controller);
    state->simulation_unit_registry_ = std::move(simulation_unit_registry);
    state->subprocess_registration_actor_ = std::move(subprocess_registration_actor);
    state->subprocess_registration_port_ = subprocess_registration_port;
    state->tasks_ = std::move(tasks);
    state->job_start_time_ = std::chrono::steady_clock::now();

    // Completed task records are appended as workers return success or failure results
    state->completed_task_records_.reserve(state->tasks_.size());

    // Task start times are filled when each task is dispatched
    state->task_start_times_.assign(state->tasks_.size(), state->job_start_time_);
    state->task_worker_ids_.resize(state->tasks_.size());

    // Workers use Task::simulation_unit_id_ to construct one fresh registered unit

    // Spawn simulation unit actors and queue their worker indices
    spawn_workers_for_tasks(self, state);

    return {// orchestrator_actor -> job_actor: start dispatching tasks for this job
            [=](caf::get_atom) -> caf::result<JobRecord> {
                if (state->response_promise_.has_value()) {
                    return caf::make_error(caf::sec::runtime_error,
                                           "job_actor only accepts one get_atom request");
                }

                // Create a deferred CAF response for the orchestrator's pending request
                state->response_promise_ = self->make_response_promise<JobRecord>();
                // Keep the response token even if initialization finishes an empty job
                const auto response = *state->response_promise_;

                // job_actor -> job_actor: enforce the configured job deadline; schedule
                // the deadline within the actor that owns all task state
                state->job_timeout_ =
                    self->mail(job_timeout_atom_v)
                        .delay(state->runtime_options_.job_request_timeout_)
                        .send(self);

                if (initialize_controller) {
                    try {
                        append_controller_tasks(
                            self, state, state->job_controller_->generate_initial_tasks());
                    } catch (const std::exception& exception) {
                        begin_job_error_shutdown(
                            self, state, "JobController error: "
                                             + std::string(exception.what()));
                        return response;
                    } catch (...) {
                        begin_job_error_shutdown(
                            self, state, "JobController error: unknown exception");
                        return response;
                    }
                }

                dispatch_tasks(self, state);

                // Return the response token that complete_job fulfils after all tasks
                // finish
                return response;
            },
            // job_actor -> job_actor: fail every remaining task at the job deadline
            [=](job_timeout_atom) {
                // A controller error takes precedence over an already queued timeout
                if (state->pending_error_message_.has_value()) {
                    return;
                }

                // Use one observed timeout time for every unfinished task
                const auto timeout_time = std::chrono::steady_clock::now();
                std::vector<bool> task_has_record(state->tasks_.size(), false);
                for (const TaskRecord& task_record : state->completed_task_records_) {
                    const std::size_t task_id =
                        static_cast<std::size_t>(task_record.task_id_);
                    task_has_record.at(task_id) = true;
                }

                for (std::size_t task_id = 0; task_id < state->tasks_.size(); ++task_id) {
                    if (task_has_record.at(task_id)) {
                        continue;
                    }
                    TaskResult task_result = make_failed_task_result(
                        "configured job timeout expired", "Job timeout");

                    // Queued tasks have no dispatch offset or elapsed execution interval
                    std::optional<std::chrono::duration<double>>
                        dispatch_offset_from_job_start;
                    std::chrono::duration<double> task_run_time{};
                    if (task_id < state->next_task_to_dispatch_) {
                        const auto task_start_time = state->task_start_times_.at(task_id);
                        dispatch_offset_from_job_start =
                            task_start_time - state->job_start_time_;
                        task_run_time = timeout_time - task_start_time;
                    }
                    state->completed_task_records_.push_back(
                        TaskRecord{state->tasks_.at(task_id), static_cast<TaskId>(task_id),
                                   std::move(task_result), task_run_time,
                                   dispatch_offset_from_job_start,
                                   state->task_worker_ids_.at(task_id)});
                }

                complete_job(self, state);
            },
            // orchestrator_actor -> job_actor: request worker shutdown
            [=](shutdown_atom) {
                state->job_timeout_.dispose();
                stop_workers(state);
                self->quit();
            }};
}

// Shared orchestrator actor that handles job submission and completion delivery
caf::behavior
orchestrator_actor(caf::event_based_actor* self, RuntimeOptions runtime_options,
                   std::uint64_t run_id,
                   std::shared_ptr<const SimulationUnitRegistry> simulation_unit_registry,
                   caf::actor subprocess_registration_actor,
                   std::uint16_t subprocess_registration_port,
                   std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs) {
    // The orchestrator retains active jobs so Runtime teardown can stop their workers
    const std::shared_ptr<std::unordered_map<JobId, caf::actor>> active_job_actors =
        std::make_shared<std::unordered_map<JobId, caf::actor>>();

    return {// Runtime -> orchestrator_actor: submit one job
            [=](caf::get_atom, JobId job_id, std::vector<Task> tasks,
                std::shared_ptr<JobController> job_controller, bool initialize_controller) {
                const caf::actor orchestrator = self;

                // Spawn one job actor for this request
                caf::actor job =
                    self->spawn(job_actor, run_id, job_id, std::move(tasks),
                                std::move(job_controller), initialize_controller,
                                runtime_options,
                                simulation_unit_registry, subprocess_registration_actor,
                                subprocess_registration_port);
                active_job_actors->emplace(job_id, job);

                // orchestrator_actor -> job_actor: request execution of one job
                self->mail(caf::get_atom_v)
                    .request(job, caf::infinite)
                    .then(
                        // job_actor -> orchestrator_actor: forward successful completion
                        [orchestrator, job_id](const JobRecord& job_record) {
                            // orchestrator_actor -> orchestrator_actor: resolve this job
                            caf::anon_mail(caf::put_atom_v, job_id, job_record)
                                .send(orchestrator);
                        },
                        // job_actor -> orchestrator_actor: report the job error
                        [job, job_id, active_job_actors, pending_jobs_mutex,
                         pending_jobs](const caf::error& err) {
                            // orchestrator_actor -> job_actor: stop after a request error
                            caf::anon_mail(shutdown_atom_v).send(job);
                            active_job_actors->erase(job_id);

                            // Remove the matching promise so it is resolved exactly once
                            std::promise<JobRecord> promise;
                            {
                                std::lock_guard<std::mutex> lock(*pending_jobs_mutex);
                                PendingJobs::iterator pending_job_entry =
                                    pending_jobs->find(job_id);
                                if (pending_job_entry == pending_jobs->end()) {
                                    return;
                                }
                                promise = std::move(pending_job_entry->second);
                                pending_jobs->erase(pending_job_entry);
                            }

                            // Actor errors are job-level failures and have no JobRecord
                            promise.set_exception(std::make_exception_ptr(
                                std::runtime_error("Job actor error: " +
                                                   caf::to_string(err))));
                        });
            },
            // orchestrator_actor -> orchestrator_actor: resolve the completed job promise
            [active_job_actors, pending_jobs_mutex, pending_jobs,
             runtime_options](caf::put_atom, JobId job_id, JobRecord job_record) mutable {
                active_job_actors->erase(job_id);

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
                // Fulfil the public JobHandle returned by Runtime::submit_job
                promise.set_value(std::move(job_record));
            },
            // Runtime -> orchestrator_actor: stop jobs and child registration
            [self, subprocess_registration_actor, active_job_actors](shutdown_atom) {
                for (const std::pair<const JobId, caf::actor>& active_job :
                     *active_job_actors) {
                    // orchestrator_actor -> job_actor: stop one active job
                    caf::anon_mail(shutdown_atom_v).urgent().send(active_job.second);
                }
                // orchestrator_actor -> subprocess_registration_actor: stop registration
                caf::anon_send_exit(subprocess_registration_actor,
                                    caf::exit_reason::user_shutdown);
                self->quit();
            }};
}

} // namespace miaow::core
