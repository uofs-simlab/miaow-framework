#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <vector>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"

// Each persistent executor owns a counter that survives successive operations
class CounterTaskExecutor : public miaow::TaskExecutor {
  public:
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        if (task.operation_ != "increment") {
            return miaow::make_failed_task_result("unknown counter operation");
        }
        count_ += caf::get<std::int64_t>(task.args_.at(0));
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "incremented",
                                 caf::config_value{count_}};
    }

  private:
    std::int64_t count_{0};
};

// Generate the last two operations using handles retained across job submission
class CounterController : public miaow::JobController {
  public:
    CounterController(miaow::SimulationUnitHandle a, miaow::SimulationUnitHandle b)
        : a_(a), b_(b) {}

    std::vector<miaow::Task>
    // With the default asynchronous policy, this gets called every time a task completes.
    // The first time, it generates the two increment tasks for the retained units, and
    // subsequent calls return an empty list.
    generate_new_tasks(std::span<const miaow::TaskRecord>) override {
        if (generated_) {
            return {};
        }
        generated_ = true;
        return {miaow::Task{a_, "increment", 4}, miaow::Task{b_, "increment", 2}};
    }

  private:
    miaow::SimulationUnitHandle a_;
    miaow::SimulationUnitHandle b_;
    bool generated_{false};
};

int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions options;
        read_example_config(argc, argv, options);
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId counter = registry.add<CounterTaskExecutor>();
        miaow::Runtime runtime(registry, options);

        // Two independent units use the same factory and retain separate executor state
        miaow::Job job;
        const miaow::SimulationUnitHandle a = job.add_unit("A", counter);
        const miaow::SimulationUnitHandle b = job.add_unit("B", counter);
        job.add_task(a, "increment", 2);
        job.add_task(a, "increment", 3);
        job.add_task(b, "increment", 1);
        job.add_task(b, "increment", 4);
        job.set_controller(std::make_unique<CounterController>(a, b));
        const miaow::JobRecord record = runtime.submit_job(std::move(job)).get();
        miaow::print_job_summary(record);
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
