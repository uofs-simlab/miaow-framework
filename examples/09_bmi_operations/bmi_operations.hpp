#pragma once

/**
 * Advance one statically linked BMI heat model through individual operations.
 */

#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "bmi_heat.hxx"

#include "miaow.hpp"
#include "miaow_bmi.hpp"

namespace miaow::examples::bmi_operations {

// Advance one heat model through two updates, querying time between operations
class HeatStepController final : public miaow::JobController {
  public:
    HeatStepController(miaow::SimulationUnitId unit, caf::config_value::dictionary config)
        : unit_{unit}, config_{std::move(config)} {}

    // Initializing the controller initializes the simulation unit
    void initialize(miaow::Job& job) override {
        job.add_task(unit_, "initialize", config_);
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskId> completed_task_ids,
                  std::span<const miaow::UnitFailure> failed_units) override {
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        const miaow::TaskId task_id = completed_task_ids.back();
        const miaow::TaskResult& result = *job.result(task_id);
        if (result.status_ != miaow::TaskStatus::succeeded) {
            return;
        }

        // Retain the sampled model times as the controller's scientific output
        if (job.task(task_id).operation_ == "get_current_time") {
            times_.push_back(result.output_);
        }

        // Each completion enables exactly one dependent operation
        constexpr std::array operations{"get_current_time", "update",
                                        "get_current_time", "update",
                                        "get_current_time", "finalize"};
        if (next_operation_ < operations.size()) {
            job.add_task(unit_, operations[next_operation_++]);
        }
    }

    caf::config_value finalize() override {
        return caf::config_value{std::move(times_)};
    }

  private:
    miaow::SimulationUnitId unit_{0};
    caf::config_value::dictionary config_;
    std::size_t next_operation_{0};
    caf::config_value::list times_;
};

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry) {
    // Each unit retains its own executor and initialized model
    const miaow::TaskExecutorId heat =
        registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
    miaow::Job job;
    const miaow::SimulationUnitId unit = job.add_unit("heat", heat);
    caf::config_value::dictionary config;
    config.insert("init_config", "../01_heat/data/configs/config_400x400.txt");
    job.set_controller(std::make_unique<HeatStepController>(unit, std::move(config)));

    // The controller submits the next operation after each successful completion
    return job;
}

} // namespace miaow::examples::bmi_operations
