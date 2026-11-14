#include <exception>
#include <iostream>
#include <utility>

#include <chrono>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../examples/config.hpp"
#include "simulation_unit_lifecycle.hpp"

int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions runtime_options;
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);
        runtime_options.subprocess_shutdown_grace_period_ = std::chrono::milliseconds{
            config.value("shutdown_grace_period_ms",
                         runtime_options.subprocess_shutdown_grace_period_.count())};
        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::tests::unit_lifecycle::make_job(registry, config);
        miaow::Runtime runtime(registry, runtime_options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord record = job_handle.get();

        miaow::print_job_summary(record);

        // Failure scenarios retain failed task records alongside successful work
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
