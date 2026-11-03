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

std::filesystem::path core::job_directory_path(const RuntimeOptions& runtime_options,
                                               std::uint64_t run_id,
                                               JobSubmissionId job_submission_id) {
    return run_directory_path(runtime_options, run_id) /
           ("job_" + std::to_string(job_submission_id));
}

// Prepare unit operations without changing previously assigned runtime fields
void core::assign_task_runtime_fields(std::vector<Task>& tasks,
                                      std::size_t first_task_index,
                                      const RuntimeOptions& runtime_options,
                                      std::uint64_t run_id,
                                      JobSubmissionId job_submission_id) {
    for (std::size_t index = first_task_index; index < tasks.size(); ++index) {
        Task& task = tasks[index];
        const std::string& feature_id = task.metadata_.at("feature_id");
        if (feature_id.find('/') != std::string::npos ||
            feature_id.find('\\') != std::string::npos) {
            throw std::runtime_error(
                "Task name error: feature_id must not contain path separators");
        }
        task.task_name_ = feature_id + "_task_" + std::to_string(index);
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
        job_directory_path(runtime_options, run_id, job_submission_id);
    const std::filesystem::path results_directory = job_directory / "results";
    std::filesystem::create_directories(results_directory);

    // Store output from every selected task in the job results directory
    for (std::size_t task_index = first_task_index; task_index < tasks.size();
         ++task_index) {
        tasks.at(task_index).task_output_dir_ = results_directory.string();
    }
}

// Initialize CAF global meta objects exactly once in a thread-safe way
static void initialize_caf_once() {
    std::call_once(caf_init_flag, [] {
        caf::core::init_global_meta_objects();
        caf::init_global_meta_objects<caf::id_block::miaow_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_internal_module>();
        caf::io::middleman::init_global_meta_objects();
    });
}

// Construct a Runtime with default runtime options
Runtime::Runtime(const TaskExecutorRegistry& task_executor_registry)
    : Runtime(task_executor_registry, RuntimeOptions{}) {}

// Construct a Runtime with one shared actor system and orchestrator
Runtime::Runtime(const TaskExecutorRegistry& task_executor_registry, RuntimeOptions options)
    : task_executor_registry_(
          std::make_shared<const TaskExecutorRegistry>(task_executor_registry)),
      runtime_options_(std::move(options)) {
    // initialize CAF global meta objects exactly once in a thread-safe way
    initialize_caf_once();

    // Require registered task executors before starting the runtime
    if (task_executor_registry_->empty()) {
        throw std::invalid_argument("Runtime requires at least one TaskExecutor");
    }
    run_id_ = allocate_run_id(runtime_options_);

    // Keep one actor system and one shared orchestrator for this Runtime
    actor_system_config_ = std::make_shared<caf::actor_system_config>();
    actor_system_config_->load<caf::io::middleman>();
    actor_system_ = std::make_shared<caf::actor_system>(*actor_system_config_);

    // Children connect back to this one Runtime-local actor through the middleman
    const caf::actor subprocess_registration_actor =
        actor_system_->spawn(caf::actor_from_state<core::SubprocessRegistration>);
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
        actor_system_->spawn(caf::actor_from_state<core::Orchestrator>, runtime_options_,
                             run_id_, task_executor_registry_,
                             subprocess_registration_actor, *subprocess_registration_port,
                             &pending_jobs_mutex_, &pending_jobs_);
}

// Request actor shutdown before releasing the shared actor system
Runtime::~Runtime() {
    if (orchestrator_) {
        // Runtime -> orchestrator_actor: stop active jobs and child registration
        caf::anon_mail(core::shutdown_atom_v).urgent().send(orchestrator_);
    }
}

Job::Job() = default;
Job::~Job() = default;
Job::Job(Job&&) = default;
Job& Job::operator=(Job&&) = default;

void JobController::callback(Job& job, std::span<const TaskRecord>,
                             std::span<const UnitFailure> failed_units) {
    if (!failed_units.empty()) {
        job.fail(failed_units.front().reason_);
    }
}

void Job::add_task(Task task) {
    if (task.unit_.job_creation_id_ != job_creation_id_ ||
        task.unit_.index_ >= units_.size() || !units_[task.unit_.index_]) {
        throw std::invalid_argument("task target does not belong to this job");
    }
    if (units_[task.unit_.index_]->state_ != UnitState::accepting_tasks || finishing_) {
        throw std::invalid_argument("task target is no longer accepting work");
    }
    tasks_.push_back(std::move(task));
}

void Job::remove_unit(SimulationUnitHandle handle) {
    if (!self_) {
        throw std::logic_error("Job::remove_unit requires a running controller hook");
    }
    if (handle.job_creation_id_ != job_creation_id_ || handle.index_ >= units_.size()) {
        throw std::invalid_argument("unit does not belong to this job");
    }
    if (units_[handle.index_]) {
        UnitBinding& unit = *units_[handle.index_];
        if (unit.state_ == UnitState::accepting_tasks) {
            unit.state_ = UnitState::removal_requested;
        } else if (unit.state_ == UnitState::failed) {
            shutdown_unit(handle.index_);
        }
    }
}

void Job::set_controller(std::unique_ptr<JobController> controller) {
    job_controller_ = std::move(controller);
}

// Prepare unit state without constructing an executor or starting an actor
SimulationUnitHandle Job::add_unit(std::string feature_id, TaskExecutorId executor_id) {
    if (feature_id.empty()) {
        throw std::invalid_argument("unit feature_id must not be empty");
    }
    for (const std::optional<UnitBinding>& unit : units_) {
        if (unit && unit->state_ == UnitState::accepting_tasks &&
            unit->feature_id_ == feature_id) {
            throw std::invalid_argument("duplicate unit feature_id: " + feature_id);
        }
    }
    const SimulationUnitHandle handle{job_creation_id_, units_.size()};
    units_.emplace_back(UnitBinding{std::move(feature_id), executor_id,
                                    SimulationUnit{executor_id}, std::nullopt});
    return handle;
}

// Copy scientific feature labels from unit bindings into newly accepted tasks
void Job::prepare_task_metadata(std::size_t first_task_index) {
    for (std::size_t index = first_task_index; index < tasks_.size(); ++index) {
        Task& task = tasks_[index];
        if (task.unit_.job_creation_id_ != job_creation_id_ ||
            task.unit_.index_ >= units_.size() || !units_[task.unit_.index_]) {
            throw std::invalid_argument("task target does not belong to this job");
        }
        const UnitBinding& unit = *units_[task.unit_.index_];
        task.metadata_["feature_id"] = unit.feature_id_;
    }
}

JobHandle Runtime::submit_job(Job job) const {
    if (!job.job_controller_) {
        throw std::invalid_argument("Runtime requires a non-null JobController");
    }

    const JobSubmissionId job_submission_id =
        next_job_submission_id_.fetch_add(1, std::memory_order_relaxed);

    // Bind initial tasks before assigning names and output directories
    job.prepare_task_metadata(0);
    core::assign_task_runtime_fields(job.tasks_, 0, runtime_options_, run_id_,
                                     job_submission_id);

    // Create a promise/future pair for this submitted job
    std::promise<JobRecord> promise;

    JobHandle handle = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
        // Register this in-progress job in the dictionary: job_submission_id -> promise
        pending_jobs_.emplace(job_submission_id, std::move(promise));
    }

    // CAF may copy local messages; the receiving actor adopts the prepared Job state
    auto shared_job = std::make_shared<Job>(std::move(job));
    caf::anon_mail(caf::get_atom_v, job_submission_id, std::move(shared_job))
        .send(orchestrator_);
    return handle;
}

} // namespace miaow
