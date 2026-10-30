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

namespace miaow::core {

// Request shutdown from every worker and any subprocess it retains
void Job::stop_workers() {
    for (const caf::actor& worker : workers_) {
        // job_actor -> simulation_unit_actor: stop work and release child resources
        caf::anon_mail(shutdown_atom_v).urgent().send(worker);
    }
}

// Add workers to match tasks without TaskRecords, capped by max_workers_
// Called during job setup and after controller-generated tasks are appended
void Job::spawn_workers_for_tasks() {
    // Each task that has succeeded or failed has one entry in completed_task_records_
    const std::size_t task_count_without_records =
        tasks_.size() - completed_task_records_.size();
    const std::size_t worker_count =
        std::min(runtime_options_.max_workers_, task_count_without_records);
    while (workers_.size() < worker_count) {
        const std::size_t worker_id = workers_.size();
        workers_.emplace_back(
            self_->spawn(caf::actor_from_state<SimulationUnit>, task_executor_registry_,
                         subprocess_registration_actor_, subprocess_registration_port_,
                         runtime_options_.task_request_timeout_));
        available_workers_.push(worker_id);
    }
}

// Prepare controller-generated tasks for dispatch using the normal runtime fields
void Job::append_controller_tasks(std::vector<Task> tasks) {
    if (tasks.empty()) {
        return;
    }
    const std::size_t first_task_index = tasks_.size();
    tasks_.reserve(first_task_index + tasks.size());
    for (Task& task : tasks) {
        tasks_.push_back(std::move(task));
    }
    assign_task_runtime_fields(tasks_, first_task_index, runtime_options_, run_id_,
                               job_id_);
    task_start_times_.resize(tasks_.size(), job_start_time_);
    task_worker_ids_.resize(tasks_.size());
    spawn_workers_for_tasks();
}

// Deliver the records currently held by the job actor and request worker shutdown
void Job::complete_job() {
    // Ignore completion attempts after the job response has already been delivered
    if (!response_promise_.has_value()) {
        return;
    }

    // Collect controller-owned output before handing the final record to the caller.
    caf::config_value output;
    try {
        output = job_controller_->finalize();
    } catch (const std::exception& exception) {
        begin_job_error_shutdown("JobController error: " + std::string(exception.what()));
        return;
    } catch (...) {
        begin_job_error_shutdown("JobController error: unknown exception");
        return;
    }

    // Keep task records in submitted task order even when workers finish out of order
    std::vector<TaskRecord> task_records = std::move(completed_task_records_);
    std::sort(task_records.begin(), task_records.end(),
              [](const TaskRecord& left, const TaskRecord& right) {
                  return left.task_id_ < right.task_id_;
              });

    // Build the final record and stop the job actor
    const std::chrono::duration<double> job_run_time =
        std::chrono::steady_clock::now() - job_start_time_;
    JobRecord job_record{run_id_, job_id_, std::move(task_records), job_run_time,
                         std::move(output)};

    // Stop job-owned work before completing the deferred request from the orchestrator
    job_timeout_.dispose();
    stop_workers();

    // Fulfil the job actor's CAF response so the orchestrator receives the JobRecord
    response_promise_->deliver(std::move(job_record));
    response_promise_.reset();
    self_->quit();
}

// Deliver a pending job-level error after all active requests have finished
void Job::complete_job_error_if_tasks_finished() {
    if (!pending_error_message_.has_value() || active_task_count_ != 0 ||
        !response_promise_.has_value()) {
        return;
    }

    // Complete the outstanding CAF request with an error for the orchestrator to forward
    response_promise_->deliver(
        caf::make_error(caf::sec::runtime_error, std::move(*pending_error_message_)));
    response_promise_.reset();
    self_->quit();
}

// Stop dispatch after a job-level error and wait for active workers to finish
void Job::begin_job_error_shutdown(std::string error_message) {
    if (pending_error_message_.has_value()) {
        return;
    }

    pending_error_message_ = std::move(error_message);
    job_timeout_.dispose();

    // Urgent shutdown runs after active execution but before queued task requests
    stop_workers();
    complete_job_error_if_tasks_finished();
}

// Complete when no queued tasks remain to dispatch and no dispatched tasks await results
void Job::complete_job_if_all_tasks_finished() {
    // There is still queued work or at least one dispatched task awaiting a result
    if (next_task_to_dispatch_ < tasks_.size() || active_task_count_ != 0) {
        return;
    }

    complete_job();
}

void Job::complete_task(std::size_t worker_id, std::size_t task_id,
                        TaskResult task_result) {
    // Task is done, so decrement active_task_count
    if (active_task_count_ > 0) {
        --active_task_count_;
    }

    // Worker slot is free again, so add it back to the available_workers queue
    available_workers_.push(worker_id);

    // Record the wall-clock duration and success/failure message for this task ID
    const std::chrono::duration<double> task_run_time =
        std::chrono::steady_clock::now() - task_start_times_.at(task_id);
    const std::chrono::duration<double> dispatch_offset_from_job_start =
        task_start_times_.at(task_id) - job_start_time_;

    // Runtime creates a task record only after the task has completed
    completed_task_records_.push_back(
        TaskRecord{tasks_.at(task_id), static_cast<TaskId>(task_id), std::move(task_result),
                   task_run_time, dispatch_offset_from_job_start, worker_id});

    // During error shutdown, record active results without generating or dispatching work
    if (pending_error_message_.has_value()) {
        complete_job_error_if_tasks_finished();
        return;
    }

    // Check whether the controller uses batch-synchronous task generation
    if (job_controller_->task_generation_policy() ==
        TaskGenerationPolicy::batch_synchronous) {
        // Check whether queued tasks remain to dispatch or there are dispatched tasks
        // awaiting results; if so, dispatch tasks and return without generating new tasks.
        if (next_task_to_dispatch_ < tasks_.size() || active_task_count_ != 0) {
            dispatch_tasks();
            return;
        }
        // If we reach this point, it means there are no queued tasks and no active tasks,
        // so we can proceed to generate new tasks. This will be a new batch of tasks.
    }

    try {
        // Deliver each completion once through a view of the new records
        const std::span<const TaskRecord> records{completed_task_records_};

        // Call the job controller to generate new tasks based on the completed task records
        std::vector<Task> generated_tasks =
            job_controller_->generate_new_tasks(records.subspan(reported_task_count_));

        // Update the count of reported tasks to include the newly completed ones
        reported_task_count_ = records.size();

        // Add the generated tasks to the queue for dispatching
        append_controller_tasks(std::move(generated_tasks));
    } catch (const std::exception& exception) {
        begin_job_error_shutdown("JobController error: " + std::string(exception.what()));
        return;
    } catch (...) {
        begin_job_error_shutdown("JobController error: unknown exception");
        return;
    }

    // Dispatch more tasks if there are any left, now that a worker slot is free
    dispatch_tasks();
}

void Job::dispatch_tasks() {
    // A controller error prevents pending tasks from entering worker mailboxes
    if (pending_error_message_.has_value()) {
        complete_job_error_if_tasks_finished();
        return;
    }

    // Dispatch tasks to available simulation unit actors until we run out of either
    while (next_task_to_dispatch_ < tasks_.size() && !available_workers_.empty()) {
        // Update next task to dispatch
        const std::size_t task_id = next_task_to_dispatch_;
        ++next_task_to_dispatch_;

        // Get an available worker slot and mark it as busy by popping from the queue
        const std::size_t worker_id = available_workers_.front();
        available_workers_.pop();

        // Task is active now that it's dispatched, so increment active_task_count
        ++active_task_count_;

        // Retain the dispatch time and worker assignment for task diagnostics
        task_start_times_.at(task_id) = std::chrono::steady_clock::now();
        task_worker_ids_.at(task_id) = worker_id;

        // SubprocessState owns its timeout so cleanup precedes the delegated response
        const caf::timespan simulation_unit_request_timeout =
            task_executor_registry_->is_subprocess(tasks_.at(task_id).task_executor_id_)
                ? caf::infinite
                : runtime_options_.task_request_timeout_;

        // job_actor -> simulation_unit_actor: request execution of one Task;
        // caf::get_atom_v is matched by caf::get_atom in the worker handler, request()
        // returns a response handle, and then() installs one response behaviour
        self_->mail(caf::get_atom_v, tasks_.at(task_id))
            .request(workers_.at(worker_id), simulation_unit_request_timeout)
            .then(
                // simulation_unit_actor -> job_actor: return the completed task result
                [this, worker_id, task_id](TaskResult task_result) {
                    complete_task(worker_id, task_id, std::move(task_result));
                },
                // simulation_unit_actor -> job_actor: report a task request error
                [this, worker_id, task_id](const caf::error& err) {
                    const TaskResult failed_task_result =
                        miaow::make_failed_task_result(caf::to_string(err), "CAF error");
                    complete_task(worker_id, task_id, failed_task_result);
                });
    }

    // Are all jobs finished? if so, deliver final results and stop the job actor
    complete_job_if_all_tasks_finished();
}

// Simulation unit actor implementation used as a worker by each job actor
SimulationUnit::SimulationUnit(
    caf::event_based_actor* self,
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
    caf::actor subprocess_registration_actor, std::uint16_t subprocess_registration_port,
    std::chrono::milliseconds task_request_timeout)
    : task_executor_registry_(std::move(task_executor_registry)),
      subprocess_state_(self, std::move(subprocess_registration_actor),
                        subprocess_registration_port, task_request_timeout) {}

caf::behavior SimulationUnit::make_behavior() {
    return {
        // job_actor -> simulation_unit_actor: execute one Task and return or defer its
        // result; caf::get_atom matches the caf::get_atom_v value sent by job_actor, and
        // CAF interprets caf::result<TaskResult> as a value, error, or delegated response
        [this](caf::get_atom, const Task& task) -> caf::result<TaskResult> {
            try {
                // Construct the registered task executor for this task
                std::unique_ptr<TaskExecutor> task_executor =
                    task_executor_registry_->make_executor(task.task_executor_id_,
                                                           subprocess_state_);
                if (!task_executor) {
                    return caf::make_error(caf::sec::runtime_error,
                                           "unknown TaskExecutorId: " +
                                               std::to_string(task.task_executor_id_));
                }
                return task_executor->execute(task);
            } catch (const std::exception& exception) {
                return caf::make_error(caf::sec::runtime_error, exception.what());
            } catch (...) {
                return caf::make_error(caf::sec::runtime_error,
                                       "unknown TaskExecutor exception");
            }
        },
        // subprocess_registration_actor -> simulation_unit_actor: deliver the child
        // handle; accept only the current launch, install CAF monitoring, and send the
        // task retained while the child process started
        [this](register_subprocess_worker_atom, SubprocessLaunchId launch_id,
               const caf::actor& subprocess_worker) {
            subprocess_state_.accept_registration(launch_id, subprocess_worker);
        },
        // simulation_unit_actor -> simulation_unit_actor: enforce the registration
        // deadline; the launch ID makes this delayed message harmless after registration,
        // failure, or replacement advances the worker to another lifecycle state
        [this](subprocess_startup_timeout_atom, SubprocessLaunchId launch_id) {
            subprocess_state_.expire_startup(launch_id);
        },
        // simulation_unit_actor -> simulation_unit_actor: poll the stopping child process;
        // each callback performs one non-blocking waitpid check and reschedules itself
        // until the child is reaped and the worker can complete its next transition
        [this](poll_subprocess_exit_atom, SubprocessLaunchId launch_id) {
            subprocess_state_.poll_for_subprocess_exit(launch_id);
        },
        // job_actor -> simulation_unit_actor: stop work and release owned resources;
        // resolve any pending task response and keep this actor alive until launch
        // reservation or child process reaping completes
        [this](shutdown_atom) { subprocess_state_.shutdown(); }};
}

// Per-job supervisor that owns simulation unit actors
Job::Job(caf::event_based_actor* self, std::uint64_t run_id, JobId job_id,
         std::vector<Task> tasks, std::shared_ptr<JobController> job_controller,
         bool initialize_controller, RuntimeOptions runtime_options,
         std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
         caf::actor subprocess_registration_actor,
         std::uint16_t subprocess_registration_port)
    : self_(self), run_id_(run_id), job_id_(job_id),
      runtime_options_(std::move(runtime_options)),
      job_controller_(std::move(job_controller)),
      initialize_controller_(initialize_controller),
      task_executor_registry_(std::move(task_executor_registry)),
      subprocess_registration_actor_(std::move(subprocess_registration_actor)),
      subprocess_registration_port_(subprocess_registration_port),
      tasks_(std::move(tasks)) {}

caf::behavior Job::make_behavior() {
    job_start_time_ = std::chrono::steady_clock::now();

    // Completed task records are appended as workers return success or failure results
    completed_task_records_.reserve(tasks_.size());

    // Task start times are filled when each task is dispatched
    task_start_times_.assign(tasks_.size(), job_start_time_);
    task_worker_ids_.resize(tasks_.size());

    // Spawn simulation unit actors and queue their worker indices
    spawn_workers_for_tasks();

    return {
        // orchestrator_actor -> job_actor: start dispatching tasks for this job
        [this](caf::get_atom) -> caf::result<JobRecord> {
            if (response_promise_.has_value()) {
                return caf::make_error(caf::sec::runtime_error,
                                       "job_actor only accepts one get_atom request");
            }

            // Create a deferred CAF response for the orchestrator's pending request
            response_promise_ = self_->make_response_promise<JobRecord>();
            // Keep the response token even if initialization finishes an empty job
            const auto response = *response_promise_;

            // job_actor -> job_actor: enforce the configured job deadline; schedule
            // the deadline within the actor that owns all task state
            job_timeout_ = self_->mail(job_timeout_atom_v)
                               .delay(runtime_options_.job_request_timeout_)
                               .send(self_);

            if (initialize_controller_) {
                try {
                    append_controller_tasks(job_controller_->generate_initial_tasks());
                } catch (const std::exception& exception) {
                    begin_job_error_shutdown("JobController error: " +
                                             std::string(exception.what()));
                    return response;
                } catch (...) {
                    begin_job_error_shutdown("JobController error: unknown exception");
                    return response;
                }
            }

            dispatch_tasks();

            // Return the response token that complete_job fulfils after all tasks
            // finish
            return response;
        },
        // job_actor -> job_actor: fail every remaining task at the job deadline
        [this](job_timeout_atom) {
            // A controller error takes precedence over an already queued timeout
            if (pending_error_message_.has_value()) {
                return;
            }

            // Use one observed timeout time for every unfinished task
            const auto timeout_time = std::chrono::steady_clock::now();
            std::vector<bool> task_has_record(tasks_.size(), false);
            for (const TaskRecord& task_record : completed_task_records_) {
                const std::size_t task_id = static_cast<std::size_t>(task_record.task_id_);
                task_has_record.at(task_id) = true;
            }

            for (std::size_t task_id = 0; task_id < tasks_.size(); ++task_id) {
                if (task_has_record.at(task_id)) {
                    continue;
                }
                TaskResult task_result =
                    make_failed_task_result("configured job timeout expired",
                                            "Job timeout");

                // Queued tasks have no dispatch offset or elapsed execution interval
                std::optional<std::chrono::duration<double>> dispatch_offset_from_job_start;
                std::chrono::duration<double> task_run_time{};
                if (task_id < next_task_to_dispatch_) {
                    const auto task_start_time = task_start_times_.at(task_id);
                    dispatch_offset_from_job_start = task_start_time - job_start_time_;
                    task_run_time = timeout_time - task_start_time;
                }
                completed_task_records_.push_back(
                    TaskRecord{tasks_.at(task_id), static_cast<TaskId>(task_id),
                               std::move(task_result), task_run_time,
                               dispatch_offset_from_job_start,
                               task_worker_ids_.at(task_id)});
            }

            complete_job();
        },
        // orchestrator_actor -> job_actor: request worker shutdown
        [this](shutdown_atom) {
            job_timeout_.dispose();
            stop_workers();
            self_->quit();
        }};
}

// Shared orchestrator actor that handles job submission and completion delivery
Orchestrator::Orchestrator(
    caf::event_based_actor* self, RuntimeOptions runtime_options, std::uint64_t run_id,
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
    caf::actor subprocess_registration_actor, std::uint16_t subprocess_registration_port,
    std::mutex* pending_jobs_mutex, PendingJobs* pending_jobs)
    : self_(self), runtime_options_(std::move(runtime_options)), run_id_(run_id),
      task_executor_registry_(std::move(task_executor_registry)),
      subprocess_registration_actor_(std::move(subprocess_registration_actor)),
      subprocess_registration_port_(subprocess_registration_port),
      pending_jobs_mutex_(pending_jobs_mutex), pending_jobs_(pending_jobs) {}

caf::behavior Orchestrator::make_behavior() {
    return {
        // Runtime -> orchestrator_actor: submit one job
        [this](caf::get_atom, JobId job_id, std::vector<Task> tasks,
               std::shared_ptr<JobController> job_controller, bool initialize_controller) {
            const caf::actor orchestrator = self_;

            // Spawn one job actor for this request
            caf::actor job =
                self_->spawn(caf::actor_from_state<Job>, run_id_, job_id, std::move(tasks),
                             std::move(job_controller), initialize_controller,
                             runtime_options_, task_executor_registry_,
                             subprocess_registration_actor_, subprocess_registration_port_);
            active_job_actors_.emplace(job_id, job);

            // orchestrator_actor -> job_actor: request execution of one job
            self_->mail(caf::get_atom_v)
                .request(job, caf::infinite)
                .then(
                    // job_actor -> orchestrator_actor: forward successful completion
                    [orchestrator, job_id](const JobRecord& job_record) {
                        // orchestrator_actor -> orchestrator_actor: resolve this job
                        caf::anon_mail(caf::put_atom_v, job_id, job_record)
                            .send(orchestrator);
                    },
                    // job_actor -> orchestrator_actor: report the job error
                    [this, job, job_id](const caf::error& err) {
                        // orchestrator_actor -> job_actor: stop after a request error
                        caf::anon_mail(shutdown_atom_v).send(job);
                        active_job_actors_.erase(job_id);

                        // Remove the matching promise so it is resolved exactly once
                        std::promise<JobRecord> promise;
                        {
                            std::lock_guard<std::mutex> lock(*pending_jobs_mutex_);
                            PendingJobs::iterator pending_job_entry =
                                pending_jobs_->find(job_id);
                            if (pending_job_entry == pending_jobs_->end()) {
                                return;
                            }
                            promise = std::move(pending_job_entry->second);
                            pending_jobs_->erase(pending_job_entry);
                        }

                        // Actor errors are job-level failures and have no JobRecord
                        promise.set_exception(std::make_exception_ptr(
                            std::runtime_error("Job actor error: " + caf::to_string(err))));
                    });
        },
        // orchestrator_actor -> orchestrator_actor: resolve the completed job promise
        [this](caf::put_atom, JobId job_id, JobRecord job_record) {
            active_job_actors_.erase(job_id);

            std::promise<JobRecord> promise;
            {
                std::lock_guard<std::mutex> lock(*pending_jobs_mutex_);
                PendingJobs::iterator pending_job_entry = pending_jobs_->find(job_id);
                if (pending_job_entry == pending_jobs_->end()) {
                    return;
                }
                promise = std::move(pending_job_entry->second);
                pending_jobs_->erase(pending_job_entry);
            } // mutex unlocks automatically here

            try {
                // Write the job record to disk if an output root is configured
                if (!runtime_options_.output_root_.empty()) {
                    const std::filesystem::path job_directory =
                        std::filesystem::path(runtime_options_.output_root_) /
                        ("run_" + std::to_string(job_record.run_id_)) /
                        ("job_" + std::to_string(job_record.job_id_));
                    std::filesystem::create_directories(job_directory);
                    write_job_record(job_directory / "job_record.json", job_record);
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
        [this](shutdown_atom) {
            for (const std::pair<const JobId, caf::actor>& active_job :
                 active_job_actors_) {
                // orchestrator_actor -> job_actor: stop one active job
                caf::anon_mail(shutdown_atom_v).urgent().send(active_job.second);
            }
            // orchestrator_actor -> subprocess_registration_actor: stop registration
            caf::anon_send_exit(subprocess_registration_actor_,
                                caf::exit_reason::user_shutdown);
            self_->quit();
        }};
}

} // namespace miaow::core
