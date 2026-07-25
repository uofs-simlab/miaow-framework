#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "miaow.hpp"

namespace {

// Sleep for the number of milliseconds stored in the task argument
class SleepSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task,
                              miaow::ExecutionContext& /*context*/) override {
        const std::string* sleep_duration = task.string_arg(0);
        if (sleep_duration == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing sleep duration"};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(*sleep_duration)));
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "finished"};
    }
};

// Generate one short task followed by one task that exceeds the job timeout
class TimeoutJobController final : public miaow::JobController {
  public:
    explicit TimeoutJobController(miaow::SimulationUnitId simulation_unit_id)
        : simulation_unit_id_{simulation_unit_id} {}

    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& /*completed_task_records*/) override {
        if (next_task_id_ >= 3) {
            return {};
        }

        const std::string sleep_duration = next_task_id_ == 1 ? "20" : "1500";
        ++next_task_id_;
        return {miaow::Task{simulation_unit_id_, sleep_duration}};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    std::size_t next_task_id_{1};
};

} // namespace

int main() {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId simulation_unit_id = registry.add<SleepSimulationUnit>();

    // Allow individual tasks to outlive the shorter whole-job deadline
    miaow::RuntimeOptions runtime_options{1, std::chrono::seconds(3),
                                          std::chrono::milliseconds(500)};
    miaow::Runtime runtime(registry, runtime_options);

    std::vector<miaow::Task> initial_tasks{miaow::Task{simulation_unit_id, "20"}};
    const miaow::JobRecord job_record =
        runtime
            .submit_job(std::move(initial_tasks),
                        std::make_unique<TimeoutJobController>(simulation_unit_id))
            .get();

    // Both completed tasks and the timed-out generated task must remain in the record
    if (job_record.tasks_.size() != 3 ||
        job_record.tasks_.at(0).result_.status_ != miaow::TaskStatus::succeeded ||
        job_record.tasks_.at(1).result_.status_ != miaow::TaskStatus::succeeded ||
        job_record.tasks_.at(2).result_.status_ != miaow::TaskStatus::failed ||
        job_record.tasks_.at(2).result_.message_ !=
            "Job timeout: configured job timeout expired") {
        std::cerr << "job timeout did not preserve generated task records\n";
        return 1;
    }

    return 0;
}
