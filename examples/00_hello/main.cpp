#include <exception>
#include <iostream>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"
#include "hello.hpp"

int main(int argc, char* argv[]) {
    // Require the example directory so relative input paths resolve correctly
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    try {
        // Default to two shared slots and let either job use both.
        miaow::RuntimeOptions runtime_options{2};
        runtime_options.set_max_total_workers(2);
        const nlohmann::json config = read_example_config(
            argc, argv, runtime_options, "data/configs/shared_workers.json");
        const nlohmann::json job_configs =
            config.is_array() ? config : nlohmann::json::array({config});

        // Prepare all jobs before the runtime takes its copy of the executor registry.
        miaow::TaskExecutorRegistry registry;
        std::vector<miaow::Job> jobs;
        jobs.reserve(job_configs.size());
        for (const nlohmann::json& job_config : job_configs) {
            jobs.push_back(miaow::examples::hello::make_job(registry, job_config));
        }
        miaow::Runtime runtime(registry, runtime_options);

        // Submit every job before waiting so their tasks can share the runtime's slots.
        std::vector<miaow::JobHandle> job_handles;
        job_handles.reserve(jobs.size());
        for (miaow::Job& job : jobs) {
            job_handles.push_back(runtime.submit_job(std::move(job)));
        }

        // Print each job's results and report failure if any task failed.
        int exit_code = 0;
        for (miaow::JobHandle& job_handle : job_handles) {
            const miaow::JobRecord job_record = job_handle.get();
            miaow::print_job_summary(job_record);
            if (!miaow::all_tasks_succeeded(job_record)) {
                exit_code = 1;
            }
        }
        return exit_code;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
