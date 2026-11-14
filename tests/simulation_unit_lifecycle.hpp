#pragma once

// Exercise unit removal and replacement through the public Job and controller APIs
// Removal drains accepted operations, rejects new work, and preserves other unit IDs
// Constructor failure reports active and queued tasks once and permits a new unit
// Callback groups distinguish asynchronous delivery from complete batch delivery
// JSON cases select the scenario, callback policy, and one or two worker slots
// Controller output records lifecycle observations alongside task results
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <sys/wait.h>

#include "miaow.hpp"
#include <nlohmann/json.hpp>

namespace miaow::tests::unit_lifecycle {

// Successive values identify retained executor state separately for each unit
class CounterTaskExecutor final : public miaow::TaskExecutor {
  public:
    // Optionally fail at first activation, before the unit executes any operation
    explicit CounterTaskExecutor(bool fail_on_construction) {
        if (fail_on_construction) {
            throw std::runtime_error("intentional executor construction failure");
        }
    }

    // Each unit returns 1, 2, ...; a replacement must start its own sequence at 1
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        if (task.operation_ == "fail") {
            return miaow::make_failed_task_result("intentional task failure");
        }
        if (task.operation_ == "sum") {
            const std::int64_t sum = caf::get<std::int64_t>(task.args_.at(0)) +
                                     caf::get<std::int64_t>(task.args_.at(1));
            return miaow::TaskResult{miaow::TaskStatus::succeeded, "inputs summed",
                                     caf::config_value{sum}};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "counter incremented",
                                 caf::config_value{++value_}};
    }

  private:
    std::int64_t value_{0};
};

// Probe whether removal or unit failure immediately closes the unit to new work
inline bool rejects_new_task(miaow::Job& job, miaow::SimulationUnitId unit) {
    try {
        job.add_task(unit, "increment");
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Preserve callback boundaries and failure counts independently of final task records
inline caf::config_value
describe_callback(std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) {
    // Copy the observations while the borrowed callback spans are valid
    caf::config_value::list task_ids;
    for (miaow::TaskId task_id : completed_task_ids) {
        task_ids.emplace_back(static_cast<std::int64_t>(task_id));
    }
    caf::config_value::dictionary observation{
        {"task_ids", caf::config_value{std::move(task_ids)}},
        {"unit_failure_count",
         caf::config_value{static_cast<std::int64_t>(failed_units.size())}}};
    return caf::config_value{std::move(observation)};
}

// Join results, overtake blocked work, and recover after dependency failure
class DependencyController final : public miaow::JobController {
  public:
    DependencyController(miaow::TaskExecutorId executor, miaow::TaskGenerationPolicy policy)
        : JobController(policy), executor_(executor) {}

    void initialize(miaow::Job& job) override {
        first_ = job.add_unit("first", executor_);
        second_ = job.add_unit("second", executor_);
        const miaow::SimulationUnitId independent = job.add_unit("independent", executor_);
        const miaow::TaskId root = job.add_task(first_, "increment");

        // Forward dependencies and undeclared output producers must fail before admission
        const auto require_rejection = [&job](miaow::Task task) {
            try {
                job.add_task(std::move(task));
            } catch (const std::invalid_argument&) {
                return;
            }
            throw std::runtime_error("invalid task dependency was accepted");
        };
        require_rejection(miaow::Task{first_, root + 1, "increment"});
        require_rejection(miaow::Task{second_, "sum", miaow::output_of(root), 9});
        miaow::Task invalid_argument{second_, root, "sum", miaow::output_of(root), 9};
        invalid_argument.args_.clear();
        require_rejection(std::move(invalid_argument));

        const miaow::TaskId forwarded =
            job.add_task(second_, root, "sum", miaow::output_of(root), 9);
        const miaow::TaskId second_increment = job.add_task(first_, forwarded, "increment");
        third_increment_ = job.add_task(first_, "increment");
        join_ =
            job.add_task(second_, std::vector<miaow::TaskId>{forwarded, second_increment},
                         "sum", miaow::output_of(forwarded),
                         miaow::output_of(second_increment));
        const miaow::TaskId failure = job.add_task(first_, join_, "fail");
        const miaow::TaskId blocked = job.add_task(second_, failure, "increment");
        terminal_ = job.add_task(first_, blocked, "increment");
        // Leave capacity for overtaking when the root enables the other unit's work
        job.add_task(independent, root, "increment");
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        callbacks_.push_back(describe_callback(completed_task_ids, failed_units));
        for (miaow::TaskId task_id : completed_task_ids) {
            if (task_id == terminal_) {
                const miaow::TaskId recovery =
                    job.add_task(first_, third_increment_, "increment");
                job.add_task(second_, std::vector<miaow::TaskId>{join_, recovery}, "sum",
                             miaow::output_of(join_), miaow::output_of(recovery));
            }
        }
    }

    caf::config_value finalize() override {
        return caf::config_value{std::move(callbacks_)};
    }

  private:
    miaow::TaskExecutorId executor_;
    miaow::SimulationUnitId first_{0};
    miaow::SimulationUnitId second_{0};
    miaow::TaskId third_increment_{0};
    miaow::TaskId join_{0};
    miaow::TaskId terminal_{0};
    caf::config_value::list callbacks_;
};

// Remove a unit with accepted work still queued, and continue using the other unit
class RemovalController final : public miaow::JobController {
  public:
    // Apply either delivery policy to the same removal workload
    RemovalController(miaow::TaskExecutorId counter_executor,
                      miaow::TaskGenerationPolicy policy)
        : JobController(policy), counter_executor_(counter_executor) {}

    // Initialization precedes dispatch, so both target operations are queued at removal
    void initialize(miaow::Job& job) override {
        const miaow::SimulationUnitId target = job.add_unit("target", counter_executor_);
        const miaow::SimulationUnitId other = job.add_unit("other", counter_executor_);

        // Tasks 0 and 1 share target state; task 2 starts the other unit's own counter
        job.add_task(target, "increment");
        job.add_task(target, "increment");
        job.add_task(other, "increment");

        // Repeated removal must be harmless and reject new work while accepted work drains
        job.remove_unit(target);
        job.remove_unit(target);
        rejected_closed_unit_ = rejects_new_task(job, target);

        // Task 3 checks that the other unit ID remains usable and its counter continues
        job.add_task(other, "increment");
    }

    // Record delivery groups; batch policy must report all four tasks in one callback
    void callback(miaow::Job&, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        callbacks_.push_back(describe_callback(completed_task_ids, failed_units));
    }

    // Save admission and delivery observations alongside the four successful task records
    caf::config_value finalize() override {
        caf::config_value::dictionary output{{"rejected_closed_unit",
                                              caf::config_value{rejected_closed_unit_}},
                                             {"callbacks",
                                              caf::config_value{std::move(callbacks_)}}};
        return caf::config_value{std::move(output)};
    }

  private:
    miaow::TaskExecutorId counter_executor_;
    caf::config_value::list callbacks_;
    bool rejected_closed_unit_{false};
};

// Recover from construction failure with a new unit and preserve the other unit's state
class ReplacementController final : public miaow::JobController {
  public:
    // Use a failing initial executor and a working executor for independent units
    ReplacementController(miaow::TaskExecutorId counter_executor,
                          miaow::TaskExecutorId failing_executor,
                          miaow::TaskGenerationPolicy policy)
        : JobController(policy), counter_executor_(counter_executor),
          failing_executor_(failing_executor) {}

    // Queue two operations on the failing unit and one on the unit that must survive
    void initialize(miaow::Job& job) override {
        target_ = job.add_unit("target", failing_executor_);
        other_ = job.add_unit("other", counter_executor_);

        // Task 0 fails during activation; queued task 1 must fail without dispatch
        // With one worker, task 2 remains queued when the constructor failure occurs
        job.add_task(target_, "increment");
        job.add_task(target_, "increment");
        job.add_task(other_, "increment");
    }

    // Recover on unit failure; batch policy groups tasks 0, 1, and 2 before replacement
    void callback(miaow::Job& job, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        // Preserve callback delivery independently of the final task-record list
        callbacks_.push_back(describe_callback(completed_task_ids, failed_units));

        // Successful asynchronous updates and the final replacement batch add no work
        if (failed_units.empty()) {
            return;
        }

        // Both failed target records must accompany the unit failure in the same callback
        failed_unit_matches_target_ = failed_units.front().unit_id_ == target_;
        for (miaow::TaskId task_id : completed_task_ids) {
            if (job.task(task_id).unit_id_ == target_ &&
                job.result(task_id)->status_ == miaow::TaskStatus::failed) {
                ++failed_tasks_reported_with_unit_failure_;
            }
        }
        rejected_closed_unit_ = rejects_new_task(job, target_);

        // Reuse the label with a new unit ID while preserving the other unit's identity
        job.remove_unit(target_);
        const miaow::SimulationUnitId replacement =
            job.add_unit("target", counter_executor_);
        replacement_has_new_id_ = replacement != target_;

        // Tasks 3 and 4 start the replacement counter; task 5 continues the other counter
        // Under batch policy these three tasks form the second and final callback group
        job.add_task(replacement, "increment");
        job.add_task(replacement, "increment");
        job.add_task(other_, "increment");
    }

    // Save failure grouping and identity observations for checks against the task records
    caf::config_value finalize() override {
        caf::config_value::dictionary output{
            {"rejected_closed_unit", caf::config_value{rejected_closed_unit_}},
            {"callbacks", caf::config_value{std::move(callbacks_)}},
            {"failed_unit_matches_target", caf::config_value{failed_unit_matches_target_}},
            {"failed_tasks_reported_with_unit_failure",
             caf::config_value{failed_tasks_reported_with_unit_failure_}},
            {"replacement_has_new_id", caf::config_value{replacement_has_new_id_}}};
        return caf::config_value{std::move(output)};
    }

  private:
    miaow::TaskExecutorId counter_executor_;
    miaow::TaskExecutorId failing_executor_;
    miaow::SimulationUnitId target_{0};
    miaow::SimulationUnitId other_{0};
    caf::config_value::list callbacks_;
    std::int64_t failed_tasks_reported_with_unit_failure_{0};
    bool rejected_closed_unit_{false};
    bool failed_unit_matches_target_{false};
    bool replacement_has_new_id_{false};
};

// Preserve a timeout while the child finishes or exceeds its cleanup grace period
class SubprocessShutdownController final : public miaow::JobController {
  public:
    SubprocessShutdownController(miaow::TaskExecutorId subprocess_executor,
                                 miaow::TaskExecutorId counter_executor,
                                 miaow::TaskGenerationPolicy policy,
                                 caf::config_value::dictionary worker_config)
        : JobController(policy), subprocess_executor_(subprocess_executor),
          counter_executor_(counter_executor), worker_config_(std::move(worker_config)) {}

    void initialize(miaow::Job& job) override {
        target_ = job.add_unit("subprocess", subprocess_executor_);
        other_ = job.add_unit("other", counter_executor_);
        job.add_task(target_, "configure", worker_config_);
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        callbacks_.push_back(describe_callback(completed_task_ids, failed_units));
        for (miaow::TaskId task_id : completed_task_ids) {
            const miaow::TaskTiming timing = job.timing(task_id);
            if (task_id == 0 &&
                job.result(task_id)->status_ == miaow::TaskStatus::succeeded) {
                child_pid_ = static_cast<pid_t>(
                    caf::get<std::int64_t>(job.result(task_id)->output_));
                job.add_task(target_, "delay");
                job.add_task(target_, "queued");
                job.add_task(other_, "increment");
            }
            if (task_id == 1 && timing.dispatch_offset_ns_ >= 0) {
                timeout_settled_offset_ =
                    std::chrono::nanoseconds{timing.completion_offset_ns_};
            }
            if (task_id == 3) {
                other_task_waited_for_shutdown_ =
                    timeout_settled_offset_ && timing.dispatch_offset_ns_ >= 0 &&
                    std::chrono::nanoseconds{timing.dispatch_offset_ns_} >=
                        *timeout_settled_offset_;
            }
        }
        if (!failed_units.empty()) {
            rejected_closed_unit_ = rejects_new_task(job, target_);

            // Only the owning parent can verify that the child was already reaped
            int wait_status = 0;
            child_reaped_before_callback_ =
                child_pid_ > 0 && ::waitpid(child_pid_, &wait_status, WNOHANG) == -1 &&
                errno == ECHILD;
        }
    }

    caf::config_value finalize() override {
        caf::config_value::dictionary output{
            {"rejected_closed_unit", caf::config_value{rejected_closed_unit_}},
            {"child_reaped_before_callback",
             caf::config_value{child_reaped_before_callback_}},
            {"other_task_waited_for_shutdown",
             caf::config_value{other_task_waited_for_shutdown_}},
            {"callbacks", caf::config_value{std::move(callbacks_)}}};
        return caf::config_value{std::move(output)};
    }

  private:
    miaow::TaskExecutorId subprocess_executor_;
    miaow::TaskExecutorId counter_executor_;
    caf::config_value::dictionary worker_config_;
    miaow::SimulationUnitId target_{0};
    miaow::SimulationUnitId other_{0};
    caf::config_value::list callbacks_;
    pid_t child_pid_{0};
    std::optional<std::chrono::nanoseconds> timeout_settled_offset_;
    bool rejected_closed_unit_{false};
    bool child_reaped_before_callback_{false};
    bool other_task_waited_for_shutdown_{false};
};

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry,
                           const nlohmann::json& config) {
    const std::string scenario_name = config.at("scenario").get<std::string>();

    // Select delivery policy without changing the scenario's workload
    const std::string policy_name = config.value("task_generation_policy", "asynchronous");
    miaow::TaskGenerationPolicy policy = miaow::TaskGenerationPolicy::asynchronous;
    if (policy_name == "batch_synchronous") {
        policy = miaow::TaskGenerationPolicy::batch_synchronous;
    } else if (policy_name != "asynchronous") {
        throw std::invalid_argument("unknown task generation policy: " + policy_name);
    }

    // Registration stores factories; executor construction happens on first task demand
    const miaow::TaskExecutorId counter = registry.add<CounterTaskExecutor>(false);
    miaow::Job job;
    if (scenario_name == "task_dependencies") {
        job.set_controller(std::make_unique<DependencyController>(counter, policy));
    } else if (scenario_name == "removal_with_queued_work") {
        job.set_controller(std::make_unique<RemovalController>(counter, policy));
    } else if (scenario_name == "replacement_after_failure") {
        const miaow::TaskExecutorId failing = registry.add<CounterTaskExecutor>(true);
        job.set_controller(
            std::make_unique<ReplacementController>(counter, failing, policy));
    } else if (scenario_name == "subprocess_shutdown") {
        caf::config_value::dictionary worker_config{
            {"execution_delay_ms",
             caf::config_value{config.value<std::int64_t>("execution_delay_ms", 1500)}},
            {"cleanup_delay_ms",
             caf::config_value{config.value<std::int64_t>("cleanup_delay_ms", 100)}}};
        const miaow::TaskExecutorId subprocess =
            registry.add_subprocess(MIAOW_SUBPROCESS_SHUTDOWN_WORKER_PATH);
        job.set_controller(
            std::make_unique<SubprocessShutdownController>(subprocess, counter, policy,
                                                           std::move(worker_config)));
    } else {
        throw std::invalid_argument("unknown unit lifecycle scenario: " + scenario_name);
    }

    return job;
}

} // namespace miaow::tests::unit_lifecycle
