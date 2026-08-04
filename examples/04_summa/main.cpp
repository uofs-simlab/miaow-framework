/**
 * This example demonstrates how to run SUMMA through MIAOW using a small NextGen-style
 * realization file.
 */
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_SUMMA_RUN_DIR)) {
        return 1;
    }

    // Use the gauge-specific output root as the default before parsing CLI overrides
    miaow::PartitionedBmiRealization realization;
    miaow::RuntimeOptions runtime_options;
    runtime_options.output_root_ = "output/gauge_01073000";
    try {
        realization = miaow::PartitionedBmiRealization(
            "data/gauge_01073000/realization_config.json");
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register the dynamic unit that executes each realization feature task
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();

    // SUMMA tasks run in subprocesses because the model has global state
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(
        dynamic_bmi, realization, miaow::ExecutionMode::persistent_subprocess);
    std::cout << "Parsed realization with " << tasks.size() << " feature task(s)\n";
    miaow::Runtime runtime(registry, runtime_options);

    // Submit a job containing the tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
