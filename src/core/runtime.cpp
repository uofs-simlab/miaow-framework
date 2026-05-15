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
    if (directory_name.rfind(run_prefix, 0) != 0 ||
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

// Get directory name for a task output, prioritizing "feature_id" metadata when available,
// and falling back to "task_{task_id}" when not. The "feature_id" value is validated to
// not contain path separators to prevent directory traversal issues.
static std::string task_output_name(const Task& task, TaskId task_id) {
    const TaskMetadata::const_iterator feature_id = task.metadata_.find("feature_id");
    if (feature_id != task.metadata_.end() && !feature_id->second.empty()) {
        if (feature_id->second.find('/') != std::string::npos ||
            feature_id->second.find('\\') != std::string::npos) {
            throw std::runtime_error(
                "Output path error: feature_id must not contain path separators");
        }
        return feature_id->second;
    }
    return "task_" + std::to_string(task_id);
}

static void assign_task_output_directories(std::vector<Task>& tasks,
                                           const RuntimeOptions& runtime_options,
                                           std::uint64_t run_id, JobId job_id) {
    if (runtime_options.output_root_.empty()) {
        for (Task& task : tasks) {
            task.task_output_dir_.clear();
            task.task_output_name_.clear();
        }
        return;
    }

    const std::filesystem::path job_directory =
        job_directory_path(runtime_options, run_id, job_id);
    const std::filesystem::path results_directory = job_directory / "results";
    std::filesystem::create_directories(results_directory);

    // Track output stems so tasks do not overwrite each other
    std::vector<std::string> assigned_output_names;
    assigned_output_names.reserve(tasks.size());

    for (std::size_t task_index = 0; task_index < tasks.size(); ++task_index) {
        Task& task = tasks.at(task_index);
        const TaskId task_id = static_cast<TaskId>(task_index);
        task.task_output_dir_ = results_directory.string();
        task.task_output_name_ = task_output_name(task, task_id);
        if (std::find(assigned_output_names.begin(), assigned_output_names.end(),
                      task.task_output_name_) != assigned_output_names.end()) {
            throw std::runtime_error("Output path error: duplicate task output name '" +
                                     task.task_output_name_ + "'");
        }
        assigned_output_names.push_back(task.task_output_name_);
    }
}

// Return a ready JobHandle for submissions that do not enter the actor runtime
static JobHandle make_ready_job_handle(JobRecord record) {
    std::promise<JobRecord> promise;
    promise.set_value(std::move(record));
    return promise.get_future();
}

// Initializes CAF global meta objects exactly once in a thread-safe way
static void initialize_caf_once() {
    std::call_once(caf_init_flag, [] {
        caf::init_global_meta_objects<caf::id_block::core_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_module>();
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
    actor_system_ = std::make_shared<caf::actor_system>(*actor_system_config_);

    // Spawn the shared orchestrator actor to handle job requests and job completion
    orchestrator_ = actor_system_->spawn(core::orchestrator_actor, runtime_options_,
                                         run_id_, simulation_unit_registry_,
                                         &pending_jobs_mutex_, &pending_jobs_);
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
    const JobId job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);

    // If there are no tasks, return a ready future with an empty result immediately
    if (tasks.empty()) {
        return make_ready_job_handle(JobRecord{run_id_, job_id});
    }

    assign_task_output_directories(tasks, runtime_options_, run_id_, job_id);

    // Create a promise/future pair for this submitted job
    std::promise<JobRecord> promise;

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
