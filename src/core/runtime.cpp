#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
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

#include "internal.hpp"

namespace miaow {

static std::once_flag caf_init_flag;
static std::atomic<std::uint64_t> next_run_id{1};

static std::filesystem::path run_directory_path(const RuntimeOptions& runtime_options,
                                                std::uint64_t run_id) {
    return std::filesystem::path(runtime_options.output_root_) /
           ("run_" + std::to_string(run_id));
}

static std::filesystem::path job_directory_path(const RuntimeOptions& runtime_options,
                                                std::uint64_t run_id,
                                                JobId job_id) {
    return run_directory_path(runtime_options, run_id) /
           ("job_" + std::to_string(job_id));
}

static void assign_task_output_directories(std::vector<Task>& tasks,
                                           const RuntimeOptions& runtime_options,
                                           std::uint64_t run_id,
                                           JobId job_id) {
    if (runtime_options.output_root_.empty()) {
        for (Task& task : tasks) {
            task.task_output_dir_.clear();
        }
        return;
    }

    std::filesystem::create_directories(run_directory_path(runtime_options, run_id));
    std::filesystem::create_directories(
        job_directory_path(runtime_options, run_id, job_id));

    for (std::size_t task_index = 0; task_index < tasks.size(); ++task_index) {
        Task& task = tasks.at(task_index);
        const std::filesystem::path task_directory =
            job_directory_path(runtime_options, run_id, job_id) /
            ("task_" + std::to_string(static_cast<TaskId>(task_index)));
        task.task_output_dir_ = task_directory.string();
        std::filesystem::create_directories(task_directory);
    }
}

// Return a ready JobHandle for empty-task submissions
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

// Validate that all task simulation unit IDs are known before starting the job
static std::optional<JobResult> validate_task_simulation_unit_ids(
    const std::vector<Task>& tasks,
    const SimulationUnitRegistry& simulation_unit_registry) {
    // Track which tasks have valid simulation unit IDs
    std::vector<bool> task_has_valid_simulation_unit_id(tasks.size(), false);
    bool has_unknown_simulation_unit_id = false; // does any task have unknown id?

    for (std::size_t task_id = 0; task_id < tasks.size(); ++task_id) {
        const Task& task = tasks.at(task_id);
        const SimulationUnitExecutor* simulation_unit_executor =
            simulation_unit_registry.find_executor(task.simulation_unit_id_);
        if (!simulation_unit_executor) {
            has_unknown_simulation_unit_id = true;
            continue;
        }
        task_has_valid_simulation_unit_id.at(task_id) = true;
    }

    if (!has_unknown_simulation_unit_id) {
        return std::nullopt;
    }

    // Create a JobResult with error messages for tasks with unknown simulation unit IDs
    JobResult job_result;
    job_result.task_results_.resize(tasks.size());
    for (std::size_t task_id = 0; task_id < tasks.size(); ++task_id) {
        TaskResult task_result;
        task_result.task_id_ = static_cast<TaskId>(task_id);
        task_result.simulation_unit_id_ = tasks.at(task_id).simulation_unit_id_;
        task_result.status_ = TaskStatus::failed;

        if (task_has_valid_simulation_unit_id.at(task_id)) {
            task_result.message_ =
                "Task " + std::to_string(task_id) + " failed: job submission rejected "
                "because another task references an unknown SimulationUnitId";
            job_result.task_results_.at(task_id) = std::move(task_result);
            continue;
        }

        task_result.message_ =
            "Task " + std::to_string(task_id) + " failed: unknown SimulationUnitId " +
            std::to_string(tasks.at(task_id).simulation_unit_id_) +
            " detected at job submission";
        job_result.task_results_.at(task_id) = std::move(task_result);
    }

    job_result.diagnostics_.task_times_s_.assign(tasks.size(), 0.0);
    return job_result;
}

// Construct a Runtime with default runtime options
Runtime::Runtime(const SimulationUnitRegistry& simulation_unit_registry)
    : Runtime(simulation_unit_registry, RuntimeOptions{}) {
}

// Construct a Runtime with one shared actor system and orchestrator
Runtime::Runtime(const SimulationUnitRegistry& simulation_unit_registry,
                 RuntimeOptions options)
    : simulation_unit_registry_(
          std::make_shared<const SimulationUnitRegistry>(simulation_unit_registry)),
      runtime_options_(std::move(options)) {
    // initialize CAF global meta objects exactly once in a thread-safe way
    initialize_caf_once();

    // Require registered units before starting the runtime
    if (simulation_unit_registry_->empty()) {
        throw std::invalid_argument("Runtime requires at least one SimulationUnit");
    }
    run_id_ = next_run_id.fetch_add(1, std::memory_order_relaxed);

    // Keep one actor system and one shared orchestrator for this Runtime
    actor_system_config_ = std::make_shared<caf::actor_system_config>();
    actor_system_ = std::make_shared<caf::actor_system>(*actor_system_config_);

    // Spawn the shared orchestrator actor to handle job requests and job completion
    orchestrator_ = actor_system_->spawn(core::orchestrator_actor,
                                         runtime_options_,
                                         run_id_,
                                         simulation_unit_registry_,
                                         &pending_jobs_mutex_,
                                         &pending_jobs_);
}

// Submitting one task is a wrapper around the multi-task submission path
JobHandle Runtime::submit_job(const Task& task) const {
    return submit_job(std::vector<Task>{task});
}

// `const&` copy path: lvalue input is copied, so caller retains ownership
JobHandle Runtime::submit_job(const std::vector<Task>& tasks) const {
    return submit_job(std::vector<Task>{tasks});
}

// `&&` move path: rvalue input transfers ownership into the Runtime message path
JobHandle Runtime::submit_job(std::vector<Task>&& tasks) const {
    JobResult job_result;

    // If there are no tasks, return a ready future with an empty result immediately
    if (tasks.empty()) {
        return make_ready_job_handle(std::move(job_result));
    }

    // Reject invalid simulation unit IDs before creating actors for this job
    std::optional<JobResult> invalid_submission_result =
        validate_task_simulation_unit_ids(tasks, *simulation_unit_registry_);
    if (invalid_submission_result.has_value()) {
        return make_ready_job_handle(std::move(invalid_submission_result.value()));
    }

    // Generate a unique job ID for this request
    const JobId job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);
    assign_task_output_directories(tasks, runtime_options_, run_id_, job_id);

    // Create a promise/future pair for this submitted job
    std::promise<JobResult> promise;

    JobHandle job = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
        // Register this in-progress job in the dictionary: job_id -> promise
        pending_jobs_.emplace(job_id, std::move(promise));
    }

    // Send this job to the shared orchestrator
    caf::anon_mail(caf::get_atom_v, job_id, std::move(tasks)).send(orchestrator_);
    return job;
}

} // namespace miaow
