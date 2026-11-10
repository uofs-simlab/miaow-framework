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

namespace miaow {

// Request actor shutdown, or release a binding with no live actor
void Job::shutdown_unit(std::size_t unit_index) {
    std::optional<UnitBinding>& unit = units_[unit_index];
    if (!unit) {
        return;
    }
    const caf::actor* simulation_unit_actor = std::get_if<caf::actor>(&unit->instance_);
    if (simulation_unit_actor && *simulation_unit_actor) {
        unit->state_ = UnitState::stopping;
        caf::anon_mail(core::shutdown_atom_v).urgent().send(*simulation_unit_actor);
    } else {
        unit.reset();
    }
}

void Job::shutdown_units() {
    for (std::size_t index = 0; index < units_.size(); ++index) {
        shutdown_unit(index);
    }
}

// Prepare tasks appended by the caller or controller before dispatch
void Job::prepare_tasks(std::size_t first_task_index) {
    if (first_task_index >= tasks_.size()) {
        return;
    }
    prepare_task_metadata(first_task_index);
    core::assign_task_runtime_fields(tasks_, first_task_index, runtime_options_, run_id_,
                                     job_submission_id_);
    task_start_times_.resize(tasks_.size());
    task_record_indices_.resize(tasks_.size());
    const bool needs_dependency_failure_check = needs_dependency_failure_check_;
    for (std::size_t index = first_task_index; index < tasks_.size(); ++index) {
        if (!record_dependency_failure(index)) {
            pending_task_indices_.push_back(index);
        }
    }

    // Earlier queued tasks cannot depend on newly appended tasks
    needs_dependency_failure_check_ = needs_dependency_failure_check;
}

void Job::fail(std::string reason) {
    if (!self_) {
        throw std::logic_error("Job::fail requires a running controller hook");
    }
    begin_job_error_shutdown("JobController error: " + std::move(reason));
}

// Wait for remaining unit actors to terminate before delivering the job result
void Job::complete_job() {
    finishing_ = true;
    job_timeout_.dispose();
    shutdown_units();
    finish_job();
}

void Job::finish_job() {
    // If the job is not in the finishing state, do not proceed with finalization
    if (!finishing_) {
        return;
    }
    // If any unit actors are still alive, defer finalization until they terminate
    for (const std::optional<UnitBinding>& unit : units_) {
        if (unit) {
            if (const caf::actor* simulation_unit_actor =
                    std::get_if<caf::actor>(&unit->instance_);
                simulation_unit_actor && *simulation_unit_actor) {
                return;
            }
        }
    }

    // Shutdown can precede the initial execution request
    if (!response_promise_) {
        self_->quit();
        return;
    }

    // If there are no error messages, attempt to finalize the job
    if (!pending_error_message_) {
        try {
            caf::config_value output = job_controller_->finalize();
            std::sort(completed_task_records_.begin(), completed_task_records_.end(),
                      [](const TaskRecord& left, const TaskRecord& right) {
                          return left.task_id_ < right.task_id_;
                      });
            const std::chrono::duration<double> run_time =
                std::chrono::steady_clock::now() - job_start_time_;
            response_promise_->deliver(JobRecord{run_id_, job_submission_id_,
                                                 std::move(completed_task_records_),
                                                 run_time, std::move(output)});
        } catch (const std::exception& exception) {
            pending_error_message_ =
                "JobController error: " + std::string(exception.what());
        } catch (...) {
            pending_error_message_ = "JobController error: unknown exception";
        }
    }
    // Fail the job if an error message is present
    if (pending_error_message_) {
        response_promise_->deliver(
            caf::make_error(caf::sec::runtime_error, std::move(*pending_error_message_)));
    }
    response_promise_.reset();
    self_->quit();
}

void Job::begin_job_error_shutdown(std::string error_message) {
    if (!pending_error_message_) {
        pending_error_message_ = std::move(error_message);
        complete_job();
    }
}

// Record each task once, including queued tasks invalidated by unit loss or a deadline
void Job::record_task(std::size_t task_index, TaskResult task_result) {
    if (task_record_indices_.at(task_index)) {
        return;
    }
    task_record_indices_[task_index] = completed_task_records_.size();
    const Task& task = tasks_[task_index];
    std::chrono::duration<double> run_time{};
    std::optional<std::chrono::duration<double>> dispatch_offset;
    if (task_start_times_[task_index]) {
        dispatch_offset = *task_start_times_[task_index] - job_start_time_;
        run_time = std::chrono::steady_clock::now() - *task_start_times_[task_index];
    }
    completed_task_records_.emplace_back(task, units_[task.unit_.index_]->task_executor_id_,
                                         static_cast<TaskId>(task_index),
                                         std::move(task_result), run_time, dispatch_offset);
    if (completed_task_records_.back().result_.status_ == TaskStatus::failed) {
        needs_dependency_failure_check_ = true;
    }
}

// Record blocked tasks in submission order to propagate dependency failures
bool Job::record_dependency_failure(std::size_t task_index) {
    for (TaskHandle dependency : tasks_[task_index].dependencies_) {
        const std::optional<std::size_t> record_index =
            task_record_indices_[dependency.task_id_];
        if (record_index &&
            completed_task_records_[*record_index].result_.status_ == TaskStatus::failed) {
            record_task(task_index,
                        make_failed_task_result(
                            "prerequisite task " + std::to_string(dependency.task_id_) +
                                " failed",
                            "Dependency error"));
            return true;
        }
    }
    return false;
}

// Fail accepted work once; hold occupied capacity until the unit actor terminates
void Job::fail_unit(std::size_t unit_index, std::string reason) {
    UnitBinding& unit = *units_[unit_index];
    if (finishing_ || unit.state_ == UnitState::failed ||
        unit.state_ == UnitState::stopping) {
        return;
    }
    unit.state_ = UnitState::failed;
    pending_unit_failures_.push_back(
        UnitFailure{SimulationUnitHandle{job_creation_id_, unit_index}, reason});
    if (unit.active_task_index_) {
        record_task(*unit.active_task_index_,
                    make_failed_task_result(reason, "Unit failure"));
    }
    std::erase_if(pending_task_indices_, [this, unit_index, &reason](std::size_t task_index) {
        if (tasks_[task_index].unit_.index_ != unit_index) {
            return false;
        }
        record_task(task_index, make_failed_task_result(reason, "Unit failure"));
        return true;
    });
    const caf::actor& simulation_unit_actor = std::get<caf::actor>(unit.instance_);
    caf::anon_mail(core::shutdown_atom_v).urgent().send(simulation_unit_actor);
}

void Job::unit_terminated(std::size_t unit_index, const caf::error& reason) {
    UnitBinding& unit = *units_[unit_index];
    fail_unit(unit_index, "simulation unit terminated: " + caf::to_string(reason));
    if (unit.active_task_index_) {
        unit.active_task_index_.reset();
        --active_task_count_;
    }
    unit.instance_ = caf::actor{};
    if (unit.state_ == UnitState::stopping) {
        units_[unit_index].reset();
    }
    advance();
}

// Prepare and dispatch controller-added tasks only after the callback returns
void Job::advance() {
    if (finishing_) {
        finish_job();
        return;
    }
    for (;;) {
        if (needs_dependency_failure_check_) {
            // Prerequisites precede dependents, so one pass propagates all failures
            std::erase_if(pending_task_indices_, [this](std::size_t task_index) {
                return record_dependency_failure(task_index);
            });
            needs_dependency_failure_check_ = false;
        }

        const bool batch_finished =
            pending_task_indices_.empty() && active_task_count_ == 0;
        const bool has_updates = reported_task_count_ < completed_task_records_.size() ||
                                 !pending_unit_failures_.empty();
        if (!has_updates || (job_controller_->task_generation_policy() ==
                                 TaskGenerationPolicy::batch_synchronous &&
                             !batch_finished)) {
            break;
        }
        try {
            const std::span<const TaskRecord> records{completed_task_records_};
            const std::size_t first_task_index = tasks_.size();
            job_controller_->callback(*this, records.subspan(reported_task_count_),
                                      pending_unit_failures_);
            reported_task_count_ = records.size();
            pending_unit_failures_.clear();
            if (finishing_) {
                return;
            }
            prepare_tasks(first_task_index);
        } catch (const std::exception& exception) {
            begin_job_error_shutdown("JobController error: " +
                                     std::string(exception.what()));
            return;
        } catch (...) {
            begin_job_error_shutdown("JobController error: unknown exception");
            return;
        }
    }

    // Drain removal requests without interrupting accepted operations
    for (std::size_t index = 0; index < units_.size(); ++index) {
        if (units_[index] && units_[index]->state_ == UnitState::removal_requested &&
            !units_[index]->active_task_index_ &&
            std::none_of(pending_task_indices_.begin(), pending_task_indices_.end(),
                         [this, index](std::size_t task_index) {
                             return tasks_[task_index].unit_.index_ == index;
                         })) {
            shutdown_unit(index);
        }
    }
    dispatch_tasks();
}

void Job::complete_task(std::size_t unit_index, std::size_t task_index,
                        TaskResult task_result) {
    if (!units_[unit_index] || units_[unit_index]->active_task_index_ != task_index ||
        task_record_indices_[task_index]) {
        return;
    }
    record_task(task_index, std::move(task_result));
    units_[unit_index]->active_task_index_.reset();
    --active_task_count_;
    advance();
}

void Job::dispatch_tasks() {
    // Stop scanning the task queue once every eligible unit is occupied
    std::size_t idle_unit_count = 0;
    for (const std::optional<UnitBinding>& unit : units_) {
        if (unit && !unit->active_task_index_ &&
            (unit->state_ == UnitState::accepting_tasks ||
             unit->state_ == UnitState::removal_requested)) {
            ++idle_unit_count;
        }
    }

    // If there are pending tasks, available workers, and idle units,
    // attempt to dispatch them
    while (!pending_task_indices_.empty() &&
           active_task_count_ < runtime_options_.max_workers() && idle_unit_count > 0) {
        // Select the first ready task, allowing it to overtake blocked tasks
        auto pending = pending_task_indices_.begin();
        for (; pending != pending_task_indices_.end(); ++pending) {
            const Task& task = tasks_[*pending];
            const std::size_t unit_index = task.unit_.index_;

            const UnitBinding& unit = *units_[unit_index];
            if (unit.active_task_index_) {
                continue;
            }
            if (unit.state_ != UnitState::accepting_tasks &&
                unit.state_ != UnitState::removal_requested) {
                continue;
            }

            // advance() has already removed tasks whose prerequisites failed
            const bool prerequisites_complete = std::all_of(
                task.dependencies_.begin(), task.dependencies_.end(),
                [this](TaskHandle prerequisite) {
                    return task_record_indices_[prerequisite.task_id_].has_value();
                });
            if (prerequisites_complete) {
                break;
            }
        }

        if (pending == pending_task_indices_.end()) {
            break;
        }
        const std::size_t task_index = *pending;
        const std::size_t unit_index = tasks_[task_index].unit_.index_;

        // Resolve snapshots in the execution copy; scheduling data stays in this job
        Task execution_task = tasks_[task_index];
        for (const auto& [argument_index, producer] : execution_task.output_references_) {
            const std::size_t record_index = *task_record_indices_[producer.task_id_];
            execution_task.args_[argument_index] =
                completed_task_records_[record_index].result_.output_;
        }
        execution_task.dependencies_.clear();
        execution_task.output_references_.clear();

        // Start the unit actor when its first task is dispatched
        UnitBinding& unit = *units_[unit_index];
        if (SimulationUnit* prepared = std::get_if<SimulationUnit>(&unit.instance_)) {
            caf::actor simulation_unit_actor =
                self_->spawn(caf::actor_from_state<SimulationUnit>, std::move(*prepared),
                             task_executor_registry_, subprocess_registration_actor_,
                             subprocess_registration_port_,
                             runtime_options_.task_request_timeout_,
                             runtime_options_.subprocess_shutdown_grace_period_);
            self_->monitor(simulation_unit_actor,
                           [this, unit_index](const caf::error& reason) {
                               unit_terminated(unit_index, reason);
                           });
            unit.instance_ = std::move(simulation_unit_actor);
        }

        // Remove the selected task from the queue and occupy one task slot
        const caf::actor& simulation_unit_actor = std::get<caf::actor>(unit.instance_);
        unit.active_task_index_ = task_index;
        pending_task_indices_.erase(pending);
        ++active_task_count_;
        --idle_unit_count;
        task_start_times_[task_index] = std::chrono::steady_clock::now();

        // Dispatch with the backend's timeout policy
        // SubprocessState owns subprocess deadlines and reports context loss after reaping
        const caf::timespan timeout =
            task_executor_registry_->is_subprocess(unit.task_executor_id_)
                ? caf::infinite
                : runtime_options_.task_request_timeout_;
        self_->mail(caf::get_atom_v, std::move(execution_task))
            .request(simulation_unit_actor, timeout)
            .then(
                [this, unit_index, task_index](TaskResult result) {
                    complete_task(unit_index, task_index, std::move(result));
                },
                [this, unit_index, task_index](const caf::error& error) {
                    if (finishing_ || task_record_indices_[task_index]) {
                        return;
                    }
                    if (error == caf::sec::request_timeout) {
                        fail_unit(unit_index, caf::to_string(error));
                        advance();
                    } else if (error != caf::sec::request_receiver_down) {
                        // The monitor records failed tasks using the actor's termination reason
                        complete_task(unit_index, task_index,
                                      make_failed_task_result(caf::to_string(error),
                                                              "CAF error"));
                    }
                });
    }

    // After dispatch, no active tasks implies no queued work remains
    if (active_task_count_ == 0) {
        complete_job();
    }
}

// Prepared state carries executor selection; CAF attaches context on first dispatch
SimulationUnit::SimulationUnit(TaskExecutorId executor_id)
    : task_executor_id_(executor_id) {}

SimulationUnit::~SimulationUnit() = default;
SimulationUnit::SimulationUnit(SimulationUnit&&) noexcept = default;
SimulationUnit& SimulationUnit::operator=(SimulationUnit&&) noexcept = default;

SimulationUnit::SimulationUnit(
    caf::event_based_actor* self, SimulationUnit&& unit,
    std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
    caf::actor subprocess_registration_actor, std::uint16_t subprocess_registration_port,
    std::chrono::milliseconds task_request_timeout,
    std::chrono::milliseconds subprocess_shutdown_grace_period)
    : SimulationUnit(std::move(unit)) {
    task_executor_registry_ = std::move(task_executor_registry);
    subprocess_state_ = std::make_unique<core::SubprocessState>(
        self, std::move(subprocess_registration_actor), subprocess_registration_port,
        task_request_timeout, subprocess_shutdown_grace_period);
}

caf::behavior SimulationUnit::make_behavior() {
    task_executor_ =
        task_executor_registry_->make_executor(task_executor_id_, *subprocess_state_);
    if (!task_executor_) {
        throw std::runtime_error("unknown TaskExecutorId: " +
                                 std::to_string(task_executor_id_));
    }
    return {
        // job_actor -> simulation_unit_actor: execute with the retained executor
        // Subprocess execution defers the response; execution exceptions fail the operation
        [this](caf::get_atom, const Task& task) -> caf::result<TaskResult> {
            try {
                return task_executor_->execute(task);
            } catch (const std::exception& exception) {
                return make_failed_task_result(exception.what());
            } catch (...) {
                return make_failed_task_result("unknown TaskExecutor exception");
            }
        },
        // Accept the current launch's child handle and send the task held during startup
        [this](core::register_subprocess_worker_atom, core::SubprocessLaunchId launch_id,
               const caf::actor& subprocess_worker_actor) {
            subprocess_state_->accept_registration(launch_id, subprocess_worker_actor);
        },
        // Enforce registration timing; ignore this deadline once startup has ended
        [this](core::subprocess_startup_timeout_atom, core::SubprocessLaunchId launch_id) {
            subprocess_state_->expire_startup(launch_id);
        },
        // Poll without blocking a scheduler thread until the child is reaped
        [this](core::poll_subprocess_exit_atom, core::SubprocessLaunchId launch_id) {
            subprocess_state_->poll_for_subprocess_exit(launch_id);
        },
        // Release the executor, then finish pending launch or child cleanup before quitting
        [this](core::shutdown_atom) {
            task_executor_.reset();
            subprocess_state_->shutdown();
        }};
}

// CAF adopts the caller's Job as its state and supplies execution context
Job::Job(caf::event_based_actor* self, std::uint64_t run_id,
         JobSubmissionId job_submission_id, std::shared_ptr<Job> job,
         RuntimeOptions runtime_options,
         std::shared_ptr<const TaskExecutorRegistry> task_executor_registry,
         caf::actor subprocess_registration_actor,
         std::uint16_t subprocess_registration_port)
    : Job(std::move(*job)) {
    self_ = self;
    run_id_ = run_id;
    job_submission_id_ = job_submission_id;
    runtime_options_ = std::move(runtime_options);
    task_executor_registry_ = std::move(task_executor_registry);
    subprocess_registration_actor_ = std::move(subprocess_registration_actor);
    subprocess_registration_port_ = subprocess_registration_port;
}

caf::behavior Job::make_behavior() {
    job_start_time_ = std::chrono::steady_clock::now();

    // Retain task results in completion order for controller callbacks
    completed_task_records_.reserve(tasks_.size());

    return {// orchestrator_actor -> job_actor: start dispatching tasks for this job
            [this](caf::get_atom) -> caf::result<JobRecord> {
                if (response_promise_.has_value()) {
                    return caf::make_error(caf::sec::runtime_error,
                                           "job_actor only accepts one get_atom request");
                }

                // Create a deferred CAF response for the orchestrator's pending request
                response_promise_ = self_->make_response_promise<JobRecord>();
                // Keep the response token even if initialization finishes an empty job
                const caf::typed_response_promise<JobRecord> response = *response_promise_;

                // Schedule the hard deadline in the actor that owns the job state
                job_timeout_ = self_->mail(core::job_timeout_atom_v)
                                   .delay(runtime_options_.job_request_timeout_)
                                   .send(self_);

                // Initialize once, then prepare caller-provided and controller-added work
                try {
                    job_controller_->initialize(*this);
                    if (pending_error_message_) {
                        return response;
                    }
                    prepare_tasks(0);
                } catch (const std::exception& exception) {
                    begin_job_error_shutdown("JobController error: " +
                                             std::string(exception.what()));
                    return response;
                } catch (...) {
                    begin_job_error_shutdown("JobController error: unknown exception");
                    return response;
                }

                advance();

                // Defer the response until tasks are recorded and unit actors terminate
                return response;
            },
            // A whole-job deadline is final; it does not invoke controller policy
            [this](core::job_timeout_atom) {
                if (finishing_) {
                    return;
                }
                for (std::size_t task_index = 0; task_index < tasks_.size(); ++task_index) {
                    if (!task_record_indices_[task_index]) {
                        record_task(task_index,
                                    make_failed_task_result(
                                        "configured job timeout expired", "Job timeout"));
                    }
                }
                complete_job();
            },
            // Runtime shutdown also waits for running handlers and subprocess reaping
            [this](core::shutdown_atom) { begin_job_error_shutdown("Runtime shutdown"); }};
}

} // namespace miaow

namespace miaow::core {

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
        [this](caf::get_atom, JobSubmissionId job_submission_id,
               std::shared_ptr<miaow::Job> prepared_job) {
            // Spawn one job actor for this request
            caf::actor job_actor =
                self_->spawn(caf::actor_from_state<miaow::Job>, run_id_, job_submission_id,
                             std::move(prepared_job), runtime_options_,
                             task_executor_registry_, subprocess_registration_actor_,
                             subprocess_registration_port_);
            active_job_actors_.emplace(job_submission_id, job_actor);

            // orchestrator_actor -> job_actor: request execution of one job
            self_->mail(caf::get_atom_v)
                .request(job_actor, caf::infinite)
                .then(
                    // job_actor -> orchestrator_actor: resolve successful completion
                    [this, job_submission_id](JobRecord job_record) {
                        active_job_actors_.erase(job_submission_id);

                        std::promise<JobRecord> promise;
                        {
                            std::lock_guard<std::mutex> lock(*pending_jobs_mutex_);
                            PendingJobs::iterator pending_job_entry =
                                pending_jobs_->find(job_submission_id);
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
                                    job_directory_path(runtime_options_, job_record.run_id_,
                                                       job_record.job_submission_id_);
                                std::filesystem::create_directories(job_directory);
                                write_job_record(job_directory / "job_record.json",
                                                 job_record);
                            }
                        } catch (...) {
                            // Output record failures are reported without changing task
                            // status
                            promise.set_exception(std::current_exception());
                            return;
                        }
                        // Fulfil the public JobHandle returned by Runtime::submit_job
                        promise.set_value(std::move(job_record));
                    },
                    // job_actor -> orchestrator_actor: report the job error
                    [this, job_actor, job_submission_id](const caf::error& err) {
                        // orchestrator_actor -> job_actor: stop after a request error
                        caf::anon_mail(shutdown_atom_v).send(job_actor);
                        active_job_actors_.erase(job_submission_id);

                        // Remove the matching promise so it is resolved exactly once
                        std::promise<JobRecord> promise;
                        {
                            std::lock_guard<std::mutex> lock(*pending_jobs_mutex_);
                            PendingJobs::iterator pending_job_entry =
                                pending_jobs_->find(job_submission_id);
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
        // Runtime -> orchestrator_actor: stop jobs and child registration
        [this](shutdown_atom) {
            for (const std::pair<const JobSubmissionId, caf::actor>& active_job :
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
