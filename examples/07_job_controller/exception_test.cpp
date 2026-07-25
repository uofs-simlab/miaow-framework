#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "miaow.hpp"

namespace {

std::atomic<std::size_t> execution_count{0};
std::atomic<bool> long_task_finished{false};

// Run two active tasks long enough to leave a third task pending
class CountingSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task,
                              miaow::ExecutionContext& /*context*/) override {
        execution_count.fetch_add(1, std::memory_order_relaxed);

        const std::string* task_kind = task.string_arg(0);
        if (task_kind == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing task kind"};
        }

        if (*task_kind == "long") {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            long_task_finished.store(true, std::memory_order_release);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return miaow::TaskResult{miaow::TaskStatus::succeeded, "finished"};
    }
};

// Report a user-defined controller failure after the initial task completes
class ThrowingJobController final : public miaow::JobController {
  public:
    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& /*completed_task_records*/) override {
        throw std::runtime_error("deliberate controller failure");
    }
};

} // namespace

int main() {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId simulation_unit_id =
        registry.add<CountingSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{2});

    miaow::JobHandle job = runtime.submit_job(
        std::vector<miaow::Task>{miaow::Task{simulation_unit_id, "short"},
                                 miaow::Task{simulation_unit_id, "long"},
                                 miaow::Task{simulation_unit_id, "pending"}},
        std::make_unique<ThrowingJobController>());

    try {
        static_cast<void>(job.get());
    } catch (const std::runtime_error& exception) {
        const std::string message = exception.what();
        if (message.find("deliberate controller failure") != std::string::npos &&
            execution_count.load(std::memory_order_relaxed) == 2 &&
            long_task_finished.load(std::memory_order_acquire)) {
            return 0;
        }
        std::cerr << "unexpected controller error: " << message << "\n";
        return 1;
    }

    std::cerr << "JobHandle::get() did not report the controller exception\n";
    return 1;
}
