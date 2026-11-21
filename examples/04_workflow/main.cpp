#include <chrono>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include "miaow.hpp"
#include "miaow_io.hpp"

namespace {

// Simulate computation while showing when each task occupies a slot
class SleepTaskExecutor final : public miaow::TaskExecutor {
  public:
    explicit SleepTaskExecutor(std::chrono::steady_clock::time_point start_time)
        : start_time_{start_time} {}

    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        // Each task carries its job label, one-based task number, and work duration
        const std::string& label = caf::get<std::string>(task.args_.at(0));
        const std::int64_t task_number = caf::get<std::int64_t>(task.args_.at(1));
        const std::chrono::milliseconds duration{caf::get<std::int64_t>(task.args_.at(2))};
        const auto print = [&](const char* event) {
            const std::chrono::milliseconds elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time_);
            // Print each line in one call and flush it for immediate display
            std::printf("%lld ms  job %s  task %lld  %s  duration=%lld ms\n",
                        static_cast<long long>(elapsed.count()), label.c_str(),
                        static_cast<long long>(task_number), event,
                        static_cast<long long>(duration.count()));
            std::fflush(stdout);
        };

        // The sleep occupies a task slot; returning makes it available to either job
        print("START");
        std::this_thread::sleep_for(duration);
        print("END");
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "sleep complete"};
    }

  private:
    std::chrono::steady_clock::time_point start_time_;
};

// The workflow coordinates job completions; each job schedules its own task chain
class TwoJobsController final : public miaow::WorkflowController {
  public:
    explicit TwoJobsController(miaow::TaskExecutorId executor_id)
        : executor_id_{executor_id} {}

    void initialize(miaow::Workflow& workflow) override {
        // Assign repeatable durations independently of task execution order
        std::mt19937 random{42};
        std::uniform_int_distribution<std::int64_t> duration_ms{100, 500};
        for (const std::string label : {"A", "B"}) {
            miaow::Job job;
            job.set_max_workers(1);
            const miaow::SimulationUnitId unit = job.add_unit(label, executor_id_);

            // Dependencies preserve task order within each job while jobs interleave
            miaow::TaskId previous =
                job.add_task(unit, "sleep", label, std::int64_t{1}, duration_ms(random));
            for (std::int64_t task_number = 2; task_number <= 6; ++task_number) {
                previous = job.add_task(unit, previous, "sleep", label, task_number,
                                        duration_ms(random));
            }
            // Submit asynchronously so neither job has to wait for the other to finish
            const miaow::WorkflowChildId child_id = workflow.submit_job(std::move(job));
            std::printf("Workflow: submitted job %s as child %llu\n", label.c_str(),
                        static_cast<unsigned long long>(child_id));
            std::fflush(stdout);
        }
    }

    void callback(miaow::Workflow& workflow, miaow::WorkflowChildId child_id,
                  const miaow::JobResult& result) override {
        // Report this child's completion while the other job may still be running
        // The base callback propagates job-level errors before the record is accessed
        WorkflowController::callback(workflow, child_id, result);
        const miaow::JobRecord& record = std::get<miaow::JobRecord>(result);
        std::printf("Workflow: child %llu finished (%zu tasks)\n",
                    static_cast<unsigned long long>(child_id), record.task_results_.size());
        std::fflush(stdout);
    }

  private:
    miaow::TaskExecutorId executor_id_;
};

} // namespace

int main(int argc, char* argv[]) {
    try {
        // Share one slot by default; --max_total_workers 2 allows the jobs to overlap
        miaow::RuntimeOptions options{1};
        options.set_max_total_workers(1);
        miaow::parse_runtime_cli_options(argc, argv, options);

        // Give each unit its own executor with a common origin for elapsed times
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId executor_id =
            registry.add<SleepTaskExecutor>(std::chrono::steady_clock::now());
        miaow::Runtime runtime(registry, options);

        // Start the workflow, which submits both jobs during initialization
        miaow::WorkflowHandle workflow_handle =
            runtime.submit_workflow(std::make_unique<TwoJobsController>(executor_id));

        // Wait outside the actor system until both jobs and their callbacks finish
        const miaow::WorkflowRecord record = workflow_handle.get();
        std::printf("Workflow finished in %.3f s\n", record.run_time_.count());
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
