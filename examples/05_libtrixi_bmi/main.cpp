/**
 * This example runs libtrixi configs through MIAOW's dynamic BMI plugin path.
 * libtrixi calls these configs libelixirs.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

namespace {
// Default number of random CFL runs to execute in this example
constexpr int default_random_cfl_runs = 12;
} // namespace

int main(int argc, char* argv[]) {
    // Check that local example data is resolved from the expected directory
    if (!miaow::ensure_expected_run_dir(MIAOW_LIBTRIXI_BMI_RUN_DIR)) {
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

    // Set the Julia context inherited by BMI subprocesses
    if (setenv("LIBTRIXI_PROJECT_DIR", LIBTRIXI_PROJECT_DIR, 0) != 0) {
        std::cerr << "failed to set LIBTRIXI_PROJECT_DIR\n";
        return 1;
    }
    if (setenv("LIBTRIXI_DEPOT_PATH", LIBTRIXI_DEPOT_PATH, 0) != 0) {
        std::cerr << "failed to set LIBTRIXI_DEPOT_PATH\n";
        return 1;
    }

    // Get the plugin path from the compile-time constant
    const std::string plugin_path = LIBTRIXI_BMI_PLUGIN_PATH;
    std::cout << "libtrixi BMI plugin: " << plugin_path << "\n";

    // Register the dynamic BMI unit used inside each subprocess
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    // Repeat the config so each task samples its own CFL number
    const miaow::DynamicBmiConfig bmi_config{
        .plugin_path_ = plugin_path,
        .init_config_ = "./advection_basic_1d.jl",
        .output_variables_ = {"simulation_time"},
        .execution_mode_ = miaow::ExecutionMode::persistent_subprocess,
    };
    const std::vector<miaow::DynamicBmiConfig> bmi_configs(
        default_random_cfl_runs, bmi_config);
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(dynamic_bmi, bmi_configs);

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
