#include <exception>
#include <iostream>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"
#include "hello.hpp"

int main(int argc, char* argv[]) {
    // Require the example directory so relative input paths resolve correctly
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    // Read optional task inputs and the common runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);

        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::examples::hello::make_job(registry, config);
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
