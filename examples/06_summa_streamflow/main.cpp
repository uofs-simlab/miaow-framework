/**
 * This example runs the Bow-at-Banff SUMMA streamflow realization through MIAOW.
 */
#include <exception>
#include <iostream>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    // Resolve generated case paths from the example directory
    if (!miaow::ensure_expected_run_dir(MIAOW_SUMMA_STREAMFLOW_RUN_DIR)) {
        return 1;
    }

    // Load the generated realization before applying runtime CLI overrides
    miaow::PartitionedBmiRealization realization;
    miaow::RuntimeOptions runtime_options;
    try {
        realization = miaow::PartitionedBmiRealization(
            "data/bow_at_banff/realization_config.json");
        runtime_options.output_root_ = realization.output_root();
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register the dynamic BMI unit used by every GRU task
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();

    // Isolate SUMMA global state in persistent subprocess workers
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(
        dynamic_bmi, realization, miaow::ExecutionMode::persistent_subprocess);
    std::cout << "Parsed realization with " << tasks.size() << " GRU task(s)\n";
    miaow::Runtime runtime(registry, runtime_options);

    // Execute the complete realization and report its task results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();
    miaow::print_task_results(job_record);
    miaow::print_diagnostics(job_record);

    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
