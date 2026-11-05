/**
 * Run one statically linked C++ BMI heat model, optionally as individual operations.
 */

#include <array>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bmi_heat.hxx" // Statically linked BMI model

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_bmi.hpp" // MIAOW API for running BMI models as task executors
#include "miaow_io.hpp"  // I/O helpers for example programs

namespace {

// Advance one heat model through two updates, querying time between operations
class HeatStepController final : public miaow::JobController {
  public:
    HeatStepController(miaow::SimulationUnitHandle unit, std::string init_config)
        : unit_{unit}, init_config_{std::move(init_config)} {}

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
        // Require the example directory so relative input paths resolve correctly
        if (!miaow::ensure_expected_run_dir(MIAOW_HEAT_RUN_DIR)) {
            return 1;
        }

        // Select the example workflow and forward the common runtime options
        bool step = false;
        std::vector<char*> runtime_arguments{argv[0]};
        for (int index = 1; index < argc; ++index) {
            if (std::string_view{argv[index]} == "--step") {
                step = true;
            } else {
                runtime_arguments.push_back(argv[index]);
            }
        }
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(static_cast<int>(runtime_arguments.size()),
                                         runtime_arguments.data(), runtime_options);

        // Select the model initialization file
        const miaow::StaticBmiConfig heat_config{
            .init_config_ = "./data/configs/config_400x400.txt",
        };

        // Register the statically linked heat model and create the runtime
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId heat =
            registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
        miaow::Runtime runtime(registry, runtime_options);

        // The controller generates individual operations; the default is a complete run
        miaow::Job job;
        const miaow::SimulationUnitHandle unit = job.add_unit("heat", heat);
        if (step) {
            job.set_controller(
                std::make_unique<HeatStepController>(unit, heat_config.init_config_));
        } else {
            job.add_task(miaow::make_bmi_task(unit, heat_config));
        }

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord job_record = job_handle.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Query results show that the model clock persists across update tasks
        if (step) {
            std::cout << "\nModel times (seconds):" << std::setprecision(10);
            for (const miaow::TaskRecord& task : job_record.tasks_) {
                if (task.operation_ == "get_current_time" &&
                    task.result_.status_ == miaow::TaskStatus::succeeded) {
                    std::cout << " " << caf::get<double>(task.result_.output_);
                }
            }
            std::cout << "\n";
        }

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
