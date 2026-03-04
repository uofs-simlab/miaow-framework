#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <caf/all.hpp>
#include <caf/ipv4_address.hpp>
#include <caf/ipv4_endpoint.hpp>
#include <caf/ipv4_subnet.hpp>
#include <caf/ipv6_address.hpp>
#include <caf/ipv6_endpoint.hpp>
#include <caf/ipv6_subnet.hpp>
#include <caf/json_array.hpp>
#include <caf/json_object.hpp>
#include <caf/json_value.hpp>
#include <caf/type_id.hpp>

#include "miaow_internal.hpp"

namespace miaow {

static std::once_flag caf_init_flag;

// Return a ready JobHandle for empty-task submissions.
static JobHandle make_ready_job_handle(JobResult result) {
    std::promise<JobResult> promise;
    promise.set_value(std::move(result));
    return promise.get_future();
}

// Initializes CAF global meta objects exactly once in a thread-safe way
static void initialize_caf_once() {
    std::call_once(caf_init_flag, [] {
        caf::init_global_meta_objects<caf::id_block::core_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_module>();
    });
}

// Construct a Runtime with one shared actor system and orchestrator.
Runtime::Runtime(SimulationUnitExecutor simulation_unit_executor, RuntimeOptions options)
    : simulation_unit_executor_(std::move(simulation_unit_executor)),
      runtime_options_(std::move(options)) {
    initialize_caf_once();
    if (!simulation_unit_executor_) {
        throw std::invalid_argument("Runtime requires a non-null SimulationUnitExecutor");
    }

    // Keep one actor system and one shared orchestrator for this Runtime.
    actor_system_config_ = std::make_shared<caf::actor_system_config>();
    actor_system_ = std::make_shared<caf::actor_system>(*actor_system_config_);

    // Spawn the shared orchestrator actor to handle job requests and job completion
    orchestrator_ = actor_system_->spawn(internal::orchestrator_actor,
                                         runtime_options_,
                                         simulation_unit_executor_,
                                         &pending_jobs_mutex_,
                                         &pending_jobs_);
}

// Submitting one task is a wrapper around the multi-task submission path
JobHandle Runtime::submit_job(const Task& task) const {
    return submit_job(std::vector<Task>{task});
}

// `const&` copy path: lvalue input is copied, so caller retains ownership.
JobHandle Runtime::submit_job(const std::vector<Task>& tasks) const {
    return submit_job(std::vector<Task>{tasks});
}

// `&&` move path: rvalue input transfers ownership into the Runtime message path.
JobHandle Runtime::submit_job(std::vector<Task>&& tasks) const {
    JobResult job_result;

    // If there are no tasks, return a ready future with an empty result immediately
    if (tasks.empty()) {
        return make_ready_job_handle(std::move(job_result));
    }

    // Generate a unique job ID for this request
    const JobId job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);

    // Create a promise/future pair for this submitted job.
    std::promise<JobResult> promise;

    JobHandle job = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
        // Register this in-progress job in the dictionary: job_id -> promise.
        pending_jobs_.emplace(job_id, std::move(promise));
    }

    // Send this job to the shared orchestrator.
    caf::anon_mail(caf::get_atom_v, job_id, std::move(tasks)).send(orchestrator_);
    return job;
}

} // namespace miaow
