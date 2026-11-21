#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "workflow.hpp"

int main(int argc, char* argv[]) {
    try {
        // Share one slot by default; --max_total_workers 2 allows the jobs to overlap
        miaow::RuntimeOptions options{1};
        options.set_max_total_workers(1);
        miaow::parse_runtime_cli_options(argc, argv, options);

        // Give each unit its own executor with a common origin for elapsed times
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId executor_id =
            registry.add<miaow::examples::workflow::SleepTaskExecutor>(
                std::chrono::steady_clock::now());
        miaow::Runtime runtime(registry, options);

        // Start the workflow, which submits both jobs during initialization
        miaow::WorkflowHandle workflow_handle = runtime.submit_workflow(
            std::make_unique<miaow::examples::workflow::TwoJobsController>(executor_id));

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
