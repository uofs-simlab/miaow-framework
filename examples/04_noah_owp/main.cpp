/**
 * This example demonstrates how to run Noah-OWP-Modular through MIAOW using a
 * realization file with one or more subdomains.
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
            ? std::string("realization.json")
            : runtime_options.realization_path_;
        realization = miaow::read_realization_config(realization_path);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    std::cout << "Parsed realization with "
              << realization.subdomains_.size()
              << " subdomain(s)\n";

    // Register Noah-OWP BMI simulation unit as a dynamic Fortran BMI plugin
    const std::string plugin_path = MIAOW_NOAH_OWP_PLUGIN_PATH;
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId noah_owp =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicFortranBmi>>(
            miaow::BmiExecution::in_subprocess,
            plugin_path);
    miaow::Runtime runtime(registry, runtime_options);

    // Build one task per realization subdomain with the subdomain config as the first
    // argument and optional MIAOW-managed forcing as the second argument.
    std::vector<miaow::Task> tasks;
    tasks.reserve(realization.subdomains_.size());
    for (const miaow::RealizationSubdomain& subdomain : realization.subdomains_) {
        if (subdomain.forcing_file_.has_value()) {
            tasks.emplace_back(noah_owp,
                               std::vector<caf::config_value>{
                                   caf::config_value{subdomain.config_path_},
                                   miaow::make_forcing_config(
                                       *subdomain.forcing_file_,
                                       subdomain.variables_names_map_),
                               });
        } else {
            tasks.emplace_back(noah_owp, subdomain.config_path_);
        }
    }

    std::cout << "Loaded Noah-OWP BMI plugin: " << plugin_path << "\n";

    // Submit tasks and wait for results
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobResult job_result = job.get();

    // Print per-task status and message in completion order
    miaow::print_task_results(job_result);

    // Print timing diagnostics for this job run
    miaow::print_diagnostics(job_result.diagnostics_);

    // Return 1 if any task failed, or 0 if all tasks succeeded
    return miaow::all_tasks_succeeded(job_result) ? 0 : 1;
}
