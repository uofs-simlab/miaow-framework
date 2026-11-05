/**
 * Run one Trixi.jl advection setup through the dynamic libtrixi BMI plugin.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    try {
        // Check that local example data is resolved from the expected directory
        if (!miaow::ensure_expected_run_dir(MIAOW_LIBTRIXI_BMI_RUN_DIR)) {
            return 1;
        }

        // Parse runtime CLI options
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

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
        // Select the plugin and Julia simulation setup
        const miaow::DynamicBmiConfig bmi_config{
            .plugin_path_ = plugin_path,
            .init_config_ = "./advection_basic_1d.jl",
        };
        std::cout << "libtrixi BMI plugin: " << plugin_path << "\n";

        // Keep the embedded Julia runtime inside the unit's dedicated child process
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId bmi_subprocess =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path());
        miaow::Runtime runtime(registry, runtime_options);

        // Prepare one advection unit and one complete simulation
        miaow::Job job;
        const miaow::SimulationUnitHandle unit = job.add_unit("advection", bmi_subprocess);
        job.add_task(miaow::make_bmi_task(unit, bmi_config));

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
