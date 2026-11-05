// Exercise unit removal and replacement through the public Job and controller APIs
// Removal drains accepted operations, rejects new work, and preserves other unit handles
// Constructor failure reports active and queued tasks once and permits a new unit
// Callback groups distinguish asynchronous delivery from complete batch delivery
// JSON cases select the scenario, callback policy, and one or two worker slots
// job_record_checks.cpp checks the observations saved by this driver
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../examples/config.hpp"

namespace {

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
    caf::result<miaow::TaskResult> execute(const miaow::Task&) override {
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "counter incremented",
                                 caf::config_value{++value_}};
    }

  private:
    std::int64_t value_{0};
};

// Probe whether removal or unit failure immediately closes the handle to new work
bool rejects_new_task(miaow::Job& job, miaow::SimulationUnitHandle unit) {
    try {
        job.add_task(unit, "increment");
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Preserve callback boundaries and failure counts independently of final task records
caf::config_value describe_callback(std::span<const miaow::TaskRecord> completed_tasks,
                                    std::span<const miaow::UnitFailure> failed_units) {
    // Copy the observations while the borrowed callback spans are valid
    caf::config_value::list task_ids;
    for (const miaow::TaskRecord& record : completed_tasks) {
        task_ids.emplace_back(static_cast<std::int64_t>(record.task_id_));
    }
    caf::config_value::dictionary observation{
        {"task_ids", caf::config_value{std::move(task_ids)}},
        {"unit_failure_count",
         caf::config_value{static_cast<std::int64_t>(failed_units.size())}}};
    return caf::config_value{std::move(observation)};
}

// Remove a unit with accepted work still queued, and continue using the other unit
class RemovalController final : public miaow::JobController {
  public:
    // Apply either delivery policy to the same removal workload
    RemovalController(miaow::TaskExecutorId counter_executor,
                      miaow::TaskGenerationPolicy policy)
        : JobController(policy), counter_executor_(counter_executor) {}

    // Initialization precedes dispatch, so both target operations are queued at removal
    void initialize(miaow::Job& job) override {
        const miaow::SimulationUnitHandle target =
            job.add_unit("target", counter_executor_);
        const miaow::SimulationUnitHandle other = job.add_unit("other", counter_executor_);

        // Tasks 0 and 1 share target state; task 2 starts the other unit's own counter
        job.add_task(target, "increment");
        job.add_task(target, "increment");
        job.add_task(other, "increment");

        // Repeated removal must be harmless and reject new work while accepted work drains
        job.remove_unit(target);
        job.remove_unit(target);
        rejected_old_handle_ = rejects_new_task(job, target);

        // Task 3 checks that the other handle remains usable and its counter continues
        job.add_task(other, "increment");
    }

    // Record delivery groups; batch policy must report all four tasks in one callback
    void callback(miaow::Job&, std::span<const miaow::TaskRecord> completed_tasks,
                  std::span<const miaow::UnitFailure> failed_units) override {
        callbacks_.push_back(describe_callback(completed_tasks, failed_units));
    }

    // Save admission and delivery observations alongside the four successful task records
    caf::config_value finalize() override {
        caf::config_value::dictionary output{{"rejected_old_handle",
                                              caf::config_value{rejected_old_handle_}},
                                             {"callbacks",
                                              caf::config_value{std::move(callbacks_)}}};
        return caf::config_value{std::move(output)};
    }

  private:
    miaow::TaskExecutorId counter_executor_;
    caf::config_value::list callbacks_;
    bool rejected_old_handle_{false};
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
    void callback(miaow::Job& job, std::span<const miaow::TaskRecord> completed_tasks,
                  std::span<const miaow::UnitFailure> failed_units) override {
        // Preserve callback delivery independently of the final task-record list
        callbacks_.push_back(describe_callback(completed_tasks, failed_units));

        // Successful asynchronous updates and the final replacement batch add no work
        if (failed_units.empty()) {
            return;
        }

        // Both failed target records must accompany the unit failure in the same callback
        failed_unit_matches_target_ = failed_units.front().unit_ == target_;
        for (const miaow::TaskRecord& record : completed_tasks) {
            if (record.unit_ == target_ &&
                record.result_.status_ == miaow::TaskStatus::failed) {
                ++failed_tasks_reported_with_unit_failure_;
            }
        }
        rejected_old_handle_ = rejects_new_task(job, target_);

        // Reuse the label with a fresh handle while preserving the other unit's identity
        job.remove_unit(target_);
        const miaow::SimulationUnitHandle replacement =
            job.add_unit("target", counter_executor_);
        replacement_has_new_handle_ = replacement != target_;

        // Tasks 3 and 4 start the replacement counter; task 5 continues the other counter
        // Under batch policy these three tasks form the second and final callback group
        job.add_task(replacement, "increment");
        job.add_task(replacement, "increment");
        job.add_task(other_, "increment");
    }

    // Save failure grouping and identity observations for checks against the task records
    caf::config_value finalize() override {
        caf::config_value::dictionary output{
            {"rejected_old_handle", caf::config_value{rejected_old_handle_}},
            {"callbacks", caf::config_value{std::move(callbacks_)}},
            {"failed_unit_matches_target", caf::config_value{failed_unit_matches_target_}},
            {"failed_tasks_reported_with_unit_failure",
             caf::config_value{failed_tasks_reported_with_unit_failure_}},
            {"replacement_has_new_handle", caf::config_value{replacement_has_new_handle_}}};
        return caf::config_value{std::move(output)};
    }

  private:
    miaow::TaskExecutorId counter_executor_;
    miaow::TaskExecutorId failing_executor_;
    miaow::SimulationUnitHandle target_;
    miaow::SimulationUnitHandle other_;
    caf::config_value::list callbacks_;
    std::int64_t failed_tasks_reported_with_unit_failure_{0};
    bool rejected_old_handle_{false};
    bool failed_unit_matches_target_{false};
    bool replacement_has_new_handle_{false};
};

} // namespace

// Run the configured workload and save records for the separate Boost.Test checker
int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions runtime_options;
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);
        const std::string scenario_name = config.at("scenario").get<std::string>();

        // Select delivery policy without changing the scenario's workload
        const std::string policy_name =
            config.value("task_generation_policy", "asynchronous");
        miaow::TaskGenerationPolicy policy = miaow::TaskGenerationPolicy::asynchronous;
        if (policy_name == "batch_synchronous") {
            policy = miaow::TaskGenerationPolicy::batch_synchronous;
        } else if (policy_name != "asynchronous") {
            throw std::invalid_argument("unknown task generation policy: " + policy_name);
        }

        // Registration stores factories; executor construction happens on first task demand
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId counter = registry.add<CounterTaskExecutor>(false);
        miaow::Job job;
        if (scenario_name == "removal_with_queued_work") {
            job.set_controller(std::make_unique<RemovalController>(counter, policy));
        } else if (scenario_name == "replacement_after_failure") {
            const miaow::TaskExecutorId failing = registry.add<CounterTaskExecutor>(true);
            job.set_controller(
                std::make_unique<ReplacementController>(counter, failing, policy));
        } else {
            throw std::invalid_argument("unknown unit lifecycle scenario: " +
                                        scenario_name);
        }

        // Runtime saves both task records and controller observations for the checker
        miaow::Runtime runtime(registry, runtime_options);
        const miaow::JobRecord record = runtime.submit_job(std::move(job)).get();
        miaow::print_job_summary(record);

        // Replacement retains the two original failures, so its expected exit code is 1
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
