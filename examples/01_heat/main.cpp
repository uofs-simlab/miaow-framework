#include <exception>
#include <iostream>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "heat.hpp"

int main(int argc, char* argv[]) {
    try {
        // Require the example directory so relative input paths resolve correctly
        if (!miaow::ensure_expected_run_dir(MIAOW_HEAT_RUN_DIR)) {
            return 1;
        }

        // Read concurrency, timeout, and output options
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::examples::heat::make_job(registry);
        miaow::Runtime runtime(registry, runtime_options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord job_record = job_handle.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
