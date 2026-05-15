/**
 * This example demonstrates how to run Noah-OWP-Modular through MIAOW using a
 * staged SYMFLUENCE realization file.
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
    if (!miaow::ensure_expected_run_dir("miaow_noah_owp", MIAOW_NOAH_OWP_RUN_DIR)) {
        return 1;
    }

    // Parse runtime CLI options and read the realization file
    miaow::RuntimeOptions runtime_options;
    miaow::RealizationConfig realization;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
        const std::string realization_path = runtime_options.realization_path_.empty()
            ? std::string("data/realization_config.json")
            : runtime_options.realization_path_;
        realization = miaow::RealizationConfig(realization_path);
        if (runtime_options.output_root_.empty() &&
            !realization.output_root_.empty()) {
            runtime_options.output_root_ = realization.output_root_;
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    std::cout << "Parsed realization with "
              << realization.feature_pipeline_configs_.size()
              << " feature pipeline config(s)\n";

    // Register dynamic BMI units and build one task per feature
    miaow::SimulationUnitRegistry registry;
    const std::vector<miaow::Task> tasks =
        miaow::build_bmi_realization_tasks(realization, registry);
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
