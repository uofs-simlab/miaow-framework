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

// Claim an unused run directory without overwriting output from earlier runtimes
static std::string allocate_run_directory(const RuntimeOptions& runtime_options) {
    if (runtime_options.output_root_.empty()) {
        return {};
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
    return run_directory_path(runtime_options, run_id).string();
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
    if (runtime_options_.caf_scheduler_threads_ == 0) {
        throw std::invalid_argument("caf_scheduler_threads must be positive");
    }
    if (runtime_options_.caf_scheduler_policy_ &&
        !core::is_scheduler_policy(*runtime_options_.caf_scheduler_policy_)) {
        throw std::invalid_argument("caf_scheduler_policy must be sharing or stealing");
    }
    run_directory_ = allocate_run_directory(runtime_options_);

    // Keep one actor system and one shared orchestrator for this Runtime
    actor_system_config_ = std::make_shared<caf::actor_system_config>();
    if (runtime_options_.caf_scheduler_threads_) {
        actor_system_config_->set("caf.scheduler.max-threads",
                                  *runtime_options_.caf_scheduler_threads_);
    }
    if (runtime_options_.caf_scheduler_policy_) {
        actor_system_config_->set("caf.scheduler.policy",
                                  *runtime_options_.caf_scheduler_policy_);
    }
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

    // Spawn the shared orchestrator for job and workflow submission and completion
    orchestrator_actor_ =
        actor_system_->spawn(caf::actor_from_state<core::Orchestrator>, runtime_options_,
                             run_directory_, task_executor_registry_,
                             subprocess_registration_actor, *subprocess_registration_port,
                             &next_job_id_, &pending_results_mutex_, &pending_jobs_,
                             &pending_workflows_);
}

// Request actor shutdown before releasing the shared actor system
Runtime::~Runtime() {
    // Take all outstanding application promises before requesting actor shutdown
    PendingJobs pending_jobs;
    PendingWorkflows pending_workflows;
    {
        std::lock_guard<std::mutex> lock(pending_results_mutex_);
        pending_jobs.swap(pending_jobs_);
        pending_workflows.swap(pending_workflows_);
    }
    for (auto& [job_id, promise] : pending_jobs) {
        promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Runtime shutdown")));
    }
    for (auto& [request_id, promise] : pending_workflows) {
        promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Runtime shutdown")));
    }

    if (orchestrator_actor_) {
        // Runtime -> orchestrator_actor: stop workflows, jobs, and child registration
        caf::anon_mail(core::shutdown_atom_v).urgent().send(orchestrator_actor_);
    }

    // Wait for unit cleanup before removing the empty diagnostic root
    orchestrator_actor_ = caf::actor{};
    actor_system_.reset();
    if (!run_directory_.empty()) {
        std::error_code ignored_error;
        std::filesystem::remove(std::filesystem::path(run_directory_) / "logs",
                                ignored_error);
    }
}

Job::Job() = default;
Job::~Job() = default;
Job::Job(Job&&) = default;
Job& Job::operator=(Job&&) = default;

void JobController::callback(Job& job, std::span<const TaskId>,
                             std::span<const UnitFailure> failed_units) {
    // If any units failed, mark the job as failed and return immediately
    if (!failed_units.empty()) {
        job.fail(failed_units.front().reason_);
    }
}

void SingleTaskPerUnitController::callback(Job& job,
                                           std::span<const TaskId> completed_task_ids,
                                           std::span<const UnitFailure> failed_units) {
    if (!failed_units.empty()) {
        job.fail(failed_units.front().reason_);
        return;
    }

    for (TaskId task_id : completed_task_ids) {
        job.remove_unit(job.task(task_id).unit_id_);
    }
}

TaskId Job::add_task(Task task) {
    if (task.unit_id_ >= units_.size()) {
        throw std::invalid_argument("task target must be an existing unit in this job");
    }
    if (units_[task.unit_id_].state_ != UnitState::accepting_tasks || finishing_) {
        throw std::invalid_argument("task target is no longer accepting work");
    }
    for (TaskId dependency : task.dependencies_) {
        if (dependency >= tasks_.size()) {
            throw std::invalid_argument(
                "task dependency must be an earlier task in this job");
        }
    }
    for (const auto& [argument_index, producer] : task.output_references_) {
        if (argument_index >= task.args_.size() ||
            std::find(task.dependencies_.begin(), task.dependencies_.end(), producer) ==
                task.dependencies_.end()) {
            throw std::invalid_argument(
                "task output reference must name an explicit dependency and argument");
        }
        if (task_data_released_[producer]) {
            throw std::invalid_argument("task output reference names released data");
        }
    }
    const TaskId task_id = static_cast<TaskId>(tasks_.size());
    tasks_.push_back(std::move(task));
    task_results_.emplace_back();
    task_data_released_.push_back(false);
    task_timings_.emplace_back();

    // Include accepted inputs before controller-added tasks are prepared for dispatch
    for (const auto& reference : tasks_.back().output_references_) {
        ++queued_output_references_[reference.second];
    }
    return task_id;
}

const Task& Job::task(TaskId task_id) const {
    return tasks_.at(task_id);
}

const std::optional<TaskResult>& Job::result(TaskId task_id) const {
    return task_results_.at(task_id);
}

void Job::release_task_data(TaskId task_id) {
    if (!self_) {
        throw std::logic_error("Job::release_task_data requires a running controller hook");
    }
    std::optional<TaskResult>& result = task_results_.at(task_id);
    if (!result) {
        throw std::logic_error("task data can only be released after completion");
    }

    // Keep the result present so completion and success dependencies still work
    Task& task = tasks_[task_id];
    std::vector<caf::config_value>{}.swap(task.args_);
    std::vector<TaskId>{}.swap(task.dependencies_);
    task.output_references_.clear();
    result->output_ = caf::config_value{};
    task_data_released_[task_id] = true;

    // Only queued output consumers can be invalidated by releasing this payload
    if (queued_output_references_.contains(task_id)) {
        needs_dependency_failure_check_ = true;
    }
}

void Job::release_after(TaskId producer, TaskId consumer) {
    release_after(producer, std::vector<TaskId>{consumer});
}

void Job::release_after(TaskId producer, std::vector<TaskId> consumers) {
    deferred_releases_.emplace_back(producer, std::move(consumers));
}

TaskTiming Job::timing(TaskId task_id) const {
    return task_timings_.at(task_id);
}

const std::string& Job::unit_label(SimulationUnitId unit_id) const {
    if (unit_id >= units_.size()) {
        throw std::invalid_argument("unit ID must name an existing unit in this job");
    }
    return units_[unit_id].feature_id_;
}

void Job::remove_unit(SimulationUnitId unit_id) {
    if (!self_) {
        throw std::logic_error("Job::remove_unit requires a running controller hook");
    }
    if (unit_id >= units_.size()) {
        throw std::invalid_argument("unit ID must name an existing unit in this job");
    }
    UnitBinding& unit = units_[unit_id];
    if (unit.state_ == UnitState::accepting_tasks) {
        unit.state_ = UnitState::removal_requested;
    } else if (unit.state_ == UnitState::failed) {
        shutdown_unit(unit_id);
    }
}

void Job::set_controller(std::unique_ptr<JobController> controller) {
    job_controller_ = std::move(controller);
}

// Prepare unit state without constructing an executor or starting an actor
SimulationUnitId Job::add_unit(std::string feature_id, TaskExecutorId executor_id) {
    if (feature_id.empty()) {
        throw std::invalid_argument("unit feature_id must not be empty");
    }
    if (feature_id.find('/') != std::string::npos ||
        feature_id.find('\\') != std::string::npos) {
        throw std::invalid_argument("unit feature_id must not contain path separators");
    }
    for (const UnitBinding& unit : units_) {
        if (unit.state_ == UnitState::accepting_tasks && unit.feature_id_ == feature_id) {
            throw std::invalid_argument("duplicate unit feature_id: " + feature_id);
        }
    }
    const SimulationUnitId unit_id = static_cast<SimulationUnitId>(units_.size());
    units_.emplace_back(
        UnitBinding{std::move(feature_id), executor_id, caf::actor{}, std::nullopt});
    return unit_id;
}

void Job::validate_for_submission() const {
    if (!job_controller_) {
        throw std::invalid_argument("Runtime requires a non-null JobController");
    }
}

JobHandle Runtime::submit_job(Job job) const {
    job.validate_for_submission();
    const JobId job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);

    // Create a promise/future pair for this submitted job
    std::promise<JobRecord> promise;

    JobHandle handle = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_results_mutex_);
        // Register this in-progress job in the dictionary: job_id -> promise
        pending_jobs_.emplace(job_id, std::move(promise));
    }

    // CAF may copy local messages; the receiving actor adopts the prepared Job state
    auto shared_job = std::make_shared<Job>(std::move(job));
    caf::anon_mail(caf::get_atom_v, job_id, std::move(shared_job))
        .send(orchestrator_actor_);
    return handle;
}

WorkflowHandle
Runtime::submit_workflow(std::unique_ptr<WorkflowController> controller) const {
    if (!controller) {
        throw std::invalid_argument("Runtime requires a non-null WorkflowController");
    }

    const core::WorkflowRequestId request_id =
        next_workflow_request_id_.fetch_add(1, std::memory_order_relaxed);
    std::promise<WorkflowRecord> promise;
    WorkflowHandle handle = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_results_mutex_);
        pending_workflows_.emplace(request_id, std::move(promise));
    }

    auto submission = std::make_shared<core::WorkflowSubmission>(
        core::WorkflowSubmission{std::move(controller)});
    // Runtime -> orchestrator_actor: start a top-level workflow
    caf::anon_mail(caf::get_atom_v, request_id, std::move(submission))
        .send(orchestrator_actor_);
    return handle;
}

} // namespace miaow
