/**
 * Run one Trixi.jl advection setup through the dynamic libtrixi BMI plugin.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

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

    const std::string plugin_path = LIBTRIXI_BMI_PLUGIN_PATH;
    std::cout << "libtrixi BMI plugin: " << plugin_path << "\n";

    // Isolate the Julia runtime in a fresh process
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId bmi_subprocess =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path(),
                                miaow::SubprocessLifetime::single_task);
    miaow::Runtime runtime(registry, runtime_options);

    // Configure one dynamic BMI task in an isolated subprocess
    const miaow::DynamicBmiConfig bmi_config{
        .plugin_path_ = plugin_path,
        .init_config_ = "./advection_basic_1d.jl",
    };
    const miaow::Task task = miaow::make_bmi_task(bmi_subprocess, bmi_config);

    // Submit a job containing the task and wait for its BMI lifecycle to complete
    miaow::JobHandle job = runtime.submit_job(task);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
