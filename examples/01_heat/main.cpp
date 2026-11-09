/**
 * Run one statically linked C++ BMI heat model through MIAOW.
 */

#include <exception>
#include <iostream>
#include <utility>

#include "bmi_heat.hxx" // Statically linked BMI model

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_bmi.hpp" // MIAOW API for running BMI models as task executors
#include "miaow_io.hpp"  // I/O helpers for example programs

int main(int argc, char* argv[]) {
    try {
        // Require the example directory so relative input paths resolve correctly
        if (!miaow::ensure_expected_run_dir(MIAOW_HEAT_RUN_DIR)) {
            return 1;
        }

        // Read concurrency, timeout, and output options
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

        // Select the model initialization file
        caf::config_value::dictionary heat_config;
        heat_config.insert("init_config", "./data/configs/config_400x400.txt");

        // Register the statically linked heat model and create the runtime
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId heat =
            registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
        miaow::Runtime runtime(registry, runtime_options);

        // Queue one complete model run on the heat unit
        miaow::Job job;
        const miaow::SimulationUnitHandle unit = job.add_unit("heat", heat);
        job.add_task(unit, "run", heat_config);

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
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
