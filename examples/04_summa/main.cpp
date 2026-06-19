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
    if (!miaow::ensure_expected_run_dir("miaow_summa", MIAOW_SUMMA_RUN_DIR)) {
        return 1;
    }

    // Ensure SUMMA can write its internal run files under the example output root
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

    // Use the packaged realization when no CLI path is provided
    const std::string realization_path = runtime_options.realization_path_.empty()
                                             ? std::string(
                                                   "data/gauge_01073000/"
                                                   "realization_config.json")
                                             : runtime_options.realization_path_;

    // Read and partition the realization file
    miaow::PartitionedBmiRealization realization;
    try {
        realization = miaow::PartitionedBmiRealization(realization_path);
        if (runtime_options.output_root_.empty() && !realization.output_root_.empty()) {
            runtime_options.output_root_ = realization.output_root_;
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    std::cout << "Parsed realization with " << realization.feature_pipeline_configs_.size()
              << " feature pipeline config(s)\n";

    // Register the pipeline model that will execute each realization feature task
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId bmi_pipeline_unit =
        registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>();

    // Build one task per feature using the registered pipeline unit
    std::vector<miaow::Task> tasks =
        miaow::build_bmi_realization_tasks(realization, bmi_pipeline_unit);
    for (miaow::Task& task : tasks) {
        // SUMMA tasks run in subprocesses because the model has global state
        task.execution_mode_ = miaow::ExecutionMode::persistent_subprocess;
    }
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
