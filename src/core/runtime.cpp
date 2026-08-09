#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <caf/all.hpp>
#include <caf/ipv4_address.hpp>
#include <caf/ipv4_endpoint.hpp>
#include <caf/ipv4_subnet.hpp>
#include <caf/ipv6_address.hpp>
#include <caf/ipv6_endpoint.hpp>
#include <caf/ipv6_subnet.hpp>
#include <caf/io/middleman.hpp>
#include <caf/json_array.hpp>
#include <caf/json_object.hpp>
#include <caf/json_value.hpp>
#include <caf/type_id.hpp>

#include "internal.hpp"

namespace miaow {

static std::once_flag caf_init_flag;
static std::atomic<std::uint64_t> next_run_id{0};

static std::filesystem::path run_directory_path(const RuntimeOptions& runtime_options,
                                                std::uint64_t run_id) {
    return std::filesystem::path(runtime_options.output_root_) /
           ("run_" + std::to_string(run_id));
}

static std::optional<std::uint64_t>
parse_run_directory_id(const std::string& directory_name) {
    constexpr const char* run_prefix = "run_";
    constexpr std::size_t run_prefix_size = 4;
    if (!directory_name.starts_with(run_prefix) ||
        directory_name.size() == run_prefix_size) {
        return std::nullopt;
    }

    std::uint64_t run_id = 0;
    const char* run_id_begin = directory_name.data() + run_prefix_size;
    const char* run_id_end = directory_name.data() + directory_name.size();
    const std::from_chars_result result = std::from_chars(run_id_begin, run_id_end, run_id);
    if (result.ec != std::errc() || result.ptr != run_id_end) {
        return std::nullopt;
    }
    return run_id;
}

// Create a unique run ID by scanning existing run directories and claiming the next one
// that is not already present
static std::uint64_t allocate_run_id(const RuntimeOptions& runtime_options) {
    if (runtime_options.output_root_.empty()) {
        return next_run_id.fetch_add(1, std::memory_order_relaxed);
    }

    const std::filesystem::path output_root(runtime_options.output_root_);
    std::filesystem::create_directories(output_root);

    std::uint64_t max_run_id = 0;
    bool found_run_id = false;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(output_root)) {
        const std::optional<std::uint64_t> run_id =
            parse_run_directory_id(entry.path().filename().string());
        if (!run_id.has_value()) {
            continue;
        }
        max_run_id = found_run_id ? std::max(max_run_id, *run_id) : *run_id;
        found_run_id = true;
    }

    // Claim the run directory in case another runtime creates one first
    std::uint64_t run_id = found_run_id ? max_run_id + 1 : 0;
    while (
        !std::filesystem::create_directory(run_directory_path(runtime_options, run_id))) {
        ++run_id;
    }
    return run_id;
}

// Get directory path for a job, which is a subdirectory of the run directory
static std::filesystem::path job_directory_path(const RuntimeOptions& runtime_options,
                                                std::uint64_t run_id, JobId job_id) {
    return run_directory_path(runtime_options, run_id) / ("job_" + std::to_string(job_id));
}

// Generate a name for a task from its metadata or vector index; first try the feature_id
// from the task's metadata, otherwise use task_<task_id> when no feature_id is available
static std::string task_name(const Task& task, TaskId task_id) {
    const TaskMetadata::const_iterator feature_id = task.metadata_.find("feature_id");
    if (feature_id != task.metadata_.end() && !feature_id->second.empty()) {
        if (feature_id->second.find('/') != std::string::npos ||
            feature_id->second.find('\\') != std::string::npos) {
            throw std::runtime_error(
                "Task name error: feature_id must not contain path separators");
        }
        return feature_id->second;
    }
    return "task_" + std::to_string(task_id);
}

// Prepare initial or generated tasks for dispatch without changing earlier task fields
void core::assign_task_runtime_fields(std::vector<Task>& tasks,
                                      std::size_t first_task_index,
                                      const RuntimeOptions& runtime_options,
                                      std::uint64_t run_id, JobId job_id) {
    // Track task names so logs and artifacts can identify tasks unambiguously
    std::vector<std::string> assigned_task_names;
    assigned_task_names.reserve(tasks.size());
    for (std::size_t task_index = 0; task_index < first_task_index; ++task_index) {
        assigned_task_names.push_back(tasks.at(task_index).task_name_);
    }

    // Assign unique names from feature_id metadata or task indices in the requested range
    for (std::size_t task_index = first_task_index; task_index < tasks.size();
         ++task_index) {
        Task& task = tasks.at(task_index);
        const TaskId task_id = static_cast<TaskId>(task_index);
        task.task_name_ = task_name(task, task_id);
        if (std::find(assigned_task_names.begin(), assigned_task_names.end(),
                      task.task_name_) != assigned_task_names.end()) {
            throw std::runtime_error("Task name error: duplicate task name '" +
                                     task.task_name_ + "'");
        }
        assigned_task_names.push_back(task.task_name_);
    }

    // Clear output paths when artifact storage is disabled
    if (runtime_options.output_root_.empty()) {
        for (std::size_t task_index = first_task_index; task_index < tasks.size();
             ++task_index) {
            tasks.at(task_index).task_output_dir_.clear();
        }
        return;
    }

    // Create the job results directory before assigning task output paths
    const std::filesystem::path job_directory =
        job_directory_path(runtime_options, run_id, job_id);
    const std::filesystem::path results_directory = job_directory / "results";
    std::filesystem::create_directories(results_directory);

    // Store output from every selected task in the job results directory
    for (std::size_t task_index = first_task_index; task_index < tasks.size();
         ++task_index) {
        tasks.at(task_index).task_output_dir_ = results_directory.string();
    }
}

// Return a ready JobHandle for submissions that do not enter the actor runtime
static JobHandle make_ready_job_handle(JobRecord record) {
    std::promise<JobRecord> promise;
    promise.set_value(std::move(record));
    return promise.get_future();
}

// Initialize CAF global meta objects exactly once in a thread-safe way
static void initialize_caf_once() {
    std::call_once(caf_init_flag, [] {
        caf::init_global_meta_objects<caf::id_block::core_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_internal_module>();
        caf::io::middleman::init_global_meta_objects();
    });
}

// Construct a Runtime with default runtime options
Runtime::Runtime(const SimulationUnitRegistry& simulation_unit_registry)
    : Runtime(simulation_unit_registry, RuntimeOptions{}) {}

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
    run_id_ = allocate_run_id(runtime_options_);

    // Keep one actor system and one shared orchestrator for this Runtime
    actor_system_config_ = std::make_shared<caf::actor_system_config>();
    actor_system_config_->load<caf::io::middleman>();
    actor_system_ = std::make_shared<caf::actor_system>(*actor_system_config_);

    // Children connect back to this one Runtime-local actor through the middleman
    const caf::actor subprocess_registration_actor =
        actor_system_->spawn(core::subprocess_registration_actor);
    const caf::expected<std::uint16_t> subprocess_registration_port =
        actor_system_->middleman().publish(subprocess_registration_actor, 0,
                                           core::subprocess_registration_host);
    if (!subprocess_registration_port) {
        // Runtime -> subprocess_registration_actor: stop after publication fails; prevent
        // the live actor from blocking actor-system destruction
        caf::anon_send_exit(subprocess_registration_actor, caf::exit_reason::user_shutdown);
        throw std::runtime_error("failed to publish subprocess registration actor: " +
                                 caf::to_string(subprocess_registration_port.error()));
    }

    // Spawn the shared orchestrator actor to handle job requests and job completion
    orchestrator_ =
        actor_system_->spawn(core::orchestrator_actor, runtime_options_, run_id_,
                             simulation_unit_registry_, subprocess_registration_actor,
                             *subprocess_registration_port, &pending_jobs_mutex_,
                             &pending_jobs_);
}

// Request actor shutdown before releasing the shared actor system
Runtime::~Runtime() {
    if (orchestrator_) {
        // Runtime -> orchestrator_actor: stop active jobs and child registration
        caf::anon_mail(core::shutdown_atom_v).urgent().send(orchestrator_);
    }
}

// Submit one task through the multi-task path
JobHandle Runtime::submit_job(const Task& task) const {
    return submit_job(std::vector<Task>{task});
}

// Submit fixed tasks with the default controller; it does not generate more tasks
JobHandle Runtime::submit_job(std::vector<Task> tasks) const {
    return submit_job(std::move(tasks), std::make_unique<JobController>());
}

// Submit initial tasks and transfer controller ownership to the job actor
JobHandle Runtime::submit_job(std::vector<Task> tasks,
                              std::unique_ptr<JobController> job_controller) const {
    if (!job_controller) {
        throw std::invalid_argument("Runtime requires a non-null JobController");
    }

    const JobId job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);

    // If there are no tasks, return a ready future with an empty result immediately
    if (tasks.empty()) {
        return make_ready_job_handle(JobRecord{run_id_, job_id});
    }

    // Assign names and output directories from index 0 because every submitted task is new
    core::assign_task_runtime_fields(tasks, 0, runtime_options_, run_id_, job_id);

    // The public interface transfers unique ownership of the controller to Runtime
    // CAF may copy message payloads, so use shared ownership while the controller passes
    // through local messages to orchestrator_actor and then job_actor
    std::shared_ptr<JobController> shared_job_controller(std::move(job_controller));

    // Create a promise/future pair for this submitted job
    std::promise<JobRecord> promise;

    JobHandle job = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
        // Register this in-progress job in the dictionary: job_id -> promise
        pending_jobs_.emplace(job_id, std::move(promise));
    }

    // Runtime -> orchestrator_actor: submit one job
    caf::anon_mail(caf::get_atom_v, job_id, std::move(tasks),
                   std::move(shared_job_controller))
        .send(orchestrator_);
    return job;
}

} // namespace miaow
