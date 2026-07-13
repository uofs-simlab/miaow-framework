/**
 * This example demonstrates how to run SUMMA through MIAOW using a small
 * NextGen-style realization file.
 */
#include <exception>
#include <filesystem>
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

    // SUMMA file managers write model-native output under the local output directory
    try {
        std::filesystem::create_directories("output");
    } catch (const std::exception& exception) {
        std::cerr << "Unable to create SUMMA output directory: " << exception.what()
                  << "\n";
        return 1;
    }

    // Parse runtime CLI options
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Read the packaged realization and use its output root unless the CLI overrides it
    miaow::PartitionedBmiRealization realization;
    try {
        realization = miaow::PartitionedBmiRealization(
            "data/gauge_01073000/realization_config.json");
        if (runtime_options.output_root_.empty() && !realization.output_root().empty()) {
            runtime_options.output_root_ = realization.output_root();
        }
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
    std::cout << "Parsed realization with " << tasks.size()
              << " feature task(s)\n";
    miaow::Runtime runtime(registry, runtime_options);

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_record);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_record);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
