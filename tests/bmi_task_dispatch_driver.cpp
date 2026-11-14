#include <exception>
#include <iostream>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../examples/config.hpp"
#include "bmi_task_dispatch.hpp"

int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions runtime_options;
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);

        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::tests::bmi_dispatch::make_job(registry, config);
        miaow::Runtime runtime(registry, runtime_options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord record = job_handle.get();

        miaow::print_job_summary(record);
        // The case JSON declares whether failed task results and exit code 1 are expected
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
