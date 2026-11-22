#include "internal.hpp"

namespace miaow {

void WorkflowController::callback(Workflow&, WorkflowChildId, const JobResult& result) {
    // A completed record is available to the controller even if some tasks failed.
    if (const auto* error = std::get_if<std::exception_ptr>(&result)) {
        std::rethrow_exception(*error);
    }
}

void WorkflowController::callback(Workflow&, WorkflowChildId, const WorkflowResult& result) {
    // By default, a failed child workflow fails its parent after outstanding work drains.
    if (const auto* error = std::get_if<std::exception_ptr>(&result)) {
        std::rethrow_exception(*error);
    }
}

Workflow::Workflow(caf::event_based_actor* self, caf::actor orchestrator_actor,
                   std::unique_ptr<WorkflowController> controller)
    : self_(self), orchestrator_actor_(std::move(orchestrator_actor)),
      controller_(std::move(controller)) {}

// Both child types use the same request protocol and share the parent's ID sequence.
template <class Value, class Submission>
WorkflowChildId Workflow::submit_child(std::shared_ptr<Submission> submission) {
    if (!accepting_children_) {
        throw std::logic_error("Workflow submissions require initialize() or callback()");
    }

    // Attach the local ID to the reply handlers. CAF serializes them with the hook
    // that made the submission, so that hook can store the ID before a reply arrives.
    using Result = std::variant<Value, std::exception_ptr>;
    const WorkflowChildId child_id = next_child_id_++;
    // workflow_actor -> orchestrator_actor: start one child on this runtime
    self_->mail(core::submit_child_atom_v, std::move(submission))
        .request(orchestrator_actor_, caf::infinite)
        .then(
            [this, child_id](Value value) {
                complete_child(child_id, Result{std::in_place_index<0>, std::move(value)});
            },
            [this, child_id](const caf::error& error) {
                complete_child(child_id, Result{std::in_place_index<1>,
                                                std::make_exception_ptr(std::runtime_error(
                                                    caf::to_string(error)))});
            });
    ++pending_children_;
    return child_id;
}

WorkflowChildId Workflow::submit_job(Job job) {
    job.validate_for_submission();
    return submit_child<JobRecord>(std::make_shared<Job>(std::move(job)));
}

WorkflowChildId Workflow::submit_workflow(std::unique_ptr<WorkflowController> controller) {
    if (!controller) {
        throw std::invalid_argument("Workflow requires a non-null WorkflowController");
    }
    auto submission = std::make_shared<core::WorkflowSubmission>(
        core::WorkflowSubmission{std::move(controller)});
    return submit_child<WorkflowRecord>(std::move(submission));
}

// Run controller policy on the workflow actor and stop invoking it after an error.
template <class Hook> void Workflow::run_hook(Hook hook) {
    if (!controller_error_) {
        accepting_children_ = true;
        try {
            hook();
        } catch (...) {
            // Keep the error while already submitted children finish normally.
            controller_error_ = std::current_exception();
        }
        accepting_children_ = false;
    }
    finish_if_idle();
}

template <class Result>
void Workflow::complete_child(WorkflowChildId child_id, const Result& result) {
    // This callback can replenish the workflow even when its last child just finished.
    --pending_children_;
    run_hook([&] { controller_->callback(*this, child_id, result); });
}

void Workflow::finish_if_idle() {
    if (pending_children_ != 0) {
        return;
    }

    // The orchestrator receives one terminal reply. Only a successful workflow
    // runs finalization and returns its output and elapsed time.
    try {
        if (controller_error_) {
            std::rethrow_exception(controller_error_);
        }
        caf::config_value output = controller_->finalize();
        const std::chrono::duration<double> run_time =
            std::chrono::steady_clock::now() - workflow_start_time_;
        response_promise_->deliver(WorkflowRecord{run_time, std::move(output)});
    } catch (const std::exception& error) {
        response_promise_->deliver(
            caf::make_error(caf::sec::runtime_error,
                            "WorkflowController error: " + std::string(error.what())));
    } catch (...) {
        response_promise_->deliver(
            caf::make_error(caf::sec::runtime_error,
                            "WorkflowController error: unknown exception"));
    }
    self_->quit();
}

caf::behavior Workflow::make_behavior() {
    workflow_start_time_ = std::chrono::steady_clock::now();
    return {// Capture the start request before invoking the first hook. An empty workflow
            // can complete during initialization and deliver the response immediately.
            [this](caf::get_atom) -> caf::result<WorkflowRecord> {
                response_promise_ = self_->make_response_promise<WorkflowRecord>();
                const caf::typed_response_promise<WorkflowRecord> response =
                    *response_promise_;
                run_hook([this] { controller_->initialize(*this); });
                return response;
            },
            // Runtime shutdown stops policy immediately. The orchestrator signals every
            // active child separately; Runtime resolves pending application futures.
            [this](core::shutdown_atom) { self_->quit(); }};
}

} // namespace miaow
