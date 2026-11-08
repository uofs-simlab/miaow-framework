/**
 * Advance one statically linked BMI heat model through individual operations.
 */

#include <array>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "bmi_heat.hxx"

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

namespace {

// Advance one heat model through two updates, querying time between operations
class HeatStepController final : public miaow::JobController {
  public:
    HeatStepController(miaow::SimulationUnitHandle unit, std::string init_config)
        : unit_{unit}, init_config_{std::move(init_config)} {}

    // Initializing the controller initializes the simulation unit
    void initialize(miaow::Job& job) override {
        job.add_task(unit_, "initialize", init_config_);
    }

    void callback(miaow::Job& job, std::span<const miaow::TaskRecord> completed_tasks,
                  std::span<const miaow::UnitFailure> failed_units) override {
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        if (completed_tasks.back().result_.status_ != miaow::TaskStatus::succeeded) {
            return;
        }

        // Each completion enables exactly one dependent operation
        constexpr std::array operations{"get_current_time", "update",
                                        "get_current_time", "update",
                                        "get_current_time", "finalize"};
        if (next_operation_ < operations.size()) {
            job.add_task(unit_, operations[next_operation_++]);
        }
    }

  private:
    miaow::SimulationUnitHandle unit_;
    std::string init_config_;
    std::size_t next_operation_{0};
};

} // namespace

int main(int argc, char* argv[]) {
    try {
        // Resolve the shared heat configuration relative to the example directory
        if (!miaow::ensure_expected_run_dir(MIAOW_BMI_OPERATIONS_RUN_DIR)) {
            return 1;
        }
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

        // Each unit retains its own executor and initialized model
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId heat =
            registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
        miaow::Runtime runtime(registry, runtime_options);
        miaow::Job job;
        const miaow::SimulationUnitHandle unit = job.add_unit("heat", heat);
        job.set_controller(std::make_unique<HeatStepController>(
            unit, "../01_heat/data/configs/config_400x400.txt"));

        // The controller submits the next operation after each successful completion
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord job_record = job_handle.get();
        miaow::print_job_summary(job_record);
        if (!miaow::all_tasks_succeeded(job_record)) {
            return 1;
        }

        // Query results show that the model clock persists across update tasks
        std::cout << "\nModel times (seconds):" << std::setprecision(10);
        for (const miaow::TaskRecord& task : job_record.tasks_) {
            if (task.operation_ == "get_current_time") {
                std::cout << " " << caf::get<double>(task.result_.output_);
            }
        }
        std::cout << "\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
