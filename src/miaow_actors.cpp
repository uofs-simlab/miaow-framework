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
                           const std::shared_ptr<JobState>& state,
                           const std::shared_ptr<JobDiagnostics>& diagnostics);

// Complete the job only when every task has been dispatched and every in-progress task has
// reported back. Otherwise, return immediately and let dispatch/completion continue
static void complete_job_if_all_tasks_finished(
    caf::event_based_actor* self,
    const std::shared_ptr<JobState>& state,
    const std::shared_ptr<JobDiagnostics>& diagnostics) {
    // No caller is waiting for a final response yet
    if (!state->response_promise_.has_value()) {
        return;
    }

    // There is still undispatched work or at least one running task
    if (state->next_task_to_dispatch_ < state->task_config_files_.size() ||
        state->active_task_count_ != 0) {
        return;
    }

    // All tasks are complete: deliver final results, clear the promise, and stop actor
    diagnostics->task_durations_s_ = state->task_durations_s_;
    diagnostics->total_duration_s_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state->job_start_time_);

    state->response_promise_->deliver(state->task_results_);
    state->response_promise_.reset();
    self->quit();
}

static void complete_task(caf::event_based_actor* self,
                          const std::shared_ptr<JobState>& state,
                          const std::shared_ptr<JobDiagnostics>& diagnostics,
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
    state->task_durations_s_.at(task_id) = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state->task_start_times_.at(task_id));

    // Record the success or failure message for this task ID in the task_results vector
    if (success) {
        state->task_results_.at(task_id) = message;
    } else {
        state->task_results_.at(task_id) = failure_message(task_id, message);
    }

    // Dispatch more tasks if there are any left, now that a worker slot is free
    dispatch_tasks(self, state, diagnostics);
}

static void dispatch_tasks(caf::event_based_actor* self,
                           const std::shared_ptr<JobState>& state,
                           const std::shared_ptr<JobDiagnostics>& diagnostics) {
    // Dispatch tasks to available simulation_unit actors until we run out of either
    while (state->next_task_to_dispatch_ < state->task_config_files_.size() &&
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

        // Forward a request to one simulation_unit actor for this task_id
        // The actor handles `caf::get_atom` and returns one result string
        // Makes call to caf::behavior simulation_unit_actor (see below)
        self->mail(caf::get_atom_v, state->task_config_files_.at(task_id))
            .request(state->workers_.at(worker_id), state->runtime_options_.request_timeout_)
            .then(
                [self, state, diagnostics, worker_id, task_id](const std::string& message) {
                    // If worker replies with message (not error), mark task as finished
                    complete_task(self, state, diagnostics, worker_id, task_id, true, message);
                },
                [self, state, diagnostics, worker_id, task_id](const caf::error& err) {
                    // If worker replies with error or times out, mark task as failed
                    complete_task(self,
                                  state,
                                  diagnostics,
                                  worker_id,
                                  task_id,
                                  false,
                                  std::string("CAF error: ") + caf::to_string(err));
                });
    }

    // Are all jobs finished? if so, deliver final results and stop the job actor
    complete_job_if_all_tasks_finished(self, state, diagnostics);
}

// Leaf simulation_unit actor in the orchestrator -> job -> simulation unit hierarchy
// These are used as workers by the job actor, which forwards workflow config files to them
caf::behavior simulation_unit_actor(caf::event_based_actor* /*self*/,
                                    SimulationUnitExecutor simulation_unit_executor) {
    return {
        [simulation_unit_executor = std::move(simulation_unit_executor)](
            caf::get_atom,
            const std::string& config_file) -> caf::result<std::string> {
            // Error if the simulation_unit_executor is not set for some reason
            if (!simulation_unit_executor) {
                return caf::make_error(caf::sec::runtime_error, "SimulationUnitExecutor is null");
            }

            // If valid simulation unit, execute its behaviour and return the result
            try {
                return simulation_unit_executor(config_file);
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
                        std::vector<std::string> config_files,
                        RuntimeOptions runtime_options,
                        SimulationUnitExecutor simulation_unit_executor,
                        std::shared_ptr<JobDiagnostics> diagnostics) {
    // JobState holds all mutable state variables for the job actor
    auto state = std::make_shared<JobState>();
    state->runtime_options_ = std::move(runtime_options);
    state->task_config_files_ = std::move(config_files);
    state->job_start_time_ = std::chrono::steady_clock::now();

    // Initialize results vector with empty strings for each task
    state->task_results_.assign(state->task_config_files_.size(), "");
    state->task_start_times_.assign(state->task_config_files_.size(), state->job_start_time_);
    state->task_durations_s_.assign(state->task_config_files_.size(),
                                    std::chrono::duration<double>::zero());

    // If max_workers > number of tasks, set worker_count to the number of tasks
    const std::size_t worker_count =
        std::min<std::size_t>(state->runtime_options_.max_workers_,
                              state->task_config_files_.size());

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
        /* When the job_actor receives a message of type get_atom, it starts dispatching
        pending tasks to available worker actors and returns all results back
        to the caller (i.e. the orchestrator) */
        [=](caf::get_atom) -> caf::result<std::vector<std::string>> {
            if (state->response_promise_.has_value()) {
                return caf::make_error(caf::sec::runtime_error,
                                       "job_actor only accepts one get_atom request");
            }

            /* The promise lets the handler return immediately while the replies are
            delivered later, keeping job_actor responsive as workers run */
            state->response_promise_ =
                self->make_response_promise<std::vector<std::string>>();

            // Start dispatching tasks to available simulation_unit actors
            dispatch_tasks(self, state, diagnostics);

            // Return to orchestrator when all tasks are complete (or errored)
            return *state->response_promise_;
        }
    };
}

caf::behavior orchestrator_actor(caf::event_based_actor* self,
                                 std::vector<std::string> config_files,
                                 RuntimeOptions runtime_options,
                                 SimulationUnitExecutor simulation_unit_executor) {
    constexpr std::size_t job_id = 0;
    auto diagnostics = std::make_shared<JobDiagnostics>();

    // Spawn a job actor to run the workflow (only one job per workflow in this version)
    caf::actor job = self->spawn(job_actor,
                                 std::move(config_files),
                                 runtime_options,
                                 std::move(simulation_unit_executor),
                                 diagnostics);

    return {
        [=](caf::get_atom) -> caf::result<std::vector<std::string>> {
            /* Similarly to the job actor, the orchestrator uses the promise pattern to
            forward replies from job_actor */
            caf::typed_response_promise<std::vector<std::string>> response_promise =
                self->make_response_promise<std::vector<std::string>>();

            /* Forward a request to the job_actor and deliver the response back to the
            caller when it arrives. Note that get_atom is the message type; _v gets the
            singleton value of that type */
            self->mail(caf::get_atom_v).request(job, runtime_options.request_timeout_)
                .then(
                    [response_promise, diagnostics](const std::vector<std::string>& messages) mutable {
                        response_promise.deliver(messages);
                        print_job_diagnostics(job_id, *diagnostics);
                    },
                    [response_promise](const caf::error& err) mutable {
                        response_promise.deliver(err);
                    });

            return response_promise;
        }
    };
}

} // namespace miaow::internal
