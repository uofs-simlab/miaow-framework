/**
 * This example demonstrates how to run Noah-OWP-Modular through MIAOW using a
 * realization file with one or more subdomains.
 */
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

namespace {

bool cli_option_requires_value(const std::string_view option_name) {
    return option_name == "--max_workers" ||
        option_name == "--task_request_timeout" ||
        option_name == "--job_request_timeout" ||
        option_name == "--output_root" ||
        option_name == "--realization";
}

void split_cli_arguments(int argc,
                         char* argv[],
                         std::vector<char*>& runtime_argv,
                         std::vector<char*>& realization_argv) {
    runtime_argv.clear();
    realization_argv.clear();
    runtime_argv.reserve(static_cast<std::size_t>(argc));
    realization_argv.reserve(static_cast<std::size_t>(argc));

    runtime_argv.push_back(argv[0]);
    realization_argv.push_back(argv[0]);

    int index = 1;
    while (index < argc) {
        const std::string_view option_name = argv[index];
        const bool route_to_realization =
            option_name == "--realization" || option_name == "--help";

        std::vector<char*>& target_argv =
            route_to_realization ? realization_argv : runtime_argv;
        target_argv.push_back(argv[index]);
        ++index;

        if (cli_option_requires_value(option_name) && index < argc) {
            target_argv.push_back(argv[index]);
            ++index;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir("miaow_noah_owp", MIAOW_NOAH_OWP_RUN_DIR)) {
        return 1;
    }

    // Parse runtime and realization CLI options
    miaow::RuntimeOptions runtime_options;
    std::string realization_path;
    miaow::RealizationConfig realization;
    std::vector<char*> runtime_argv;
    std::vector<char*> realization_argv;
    split_cli_arguments(argc, argv, runtime_argv, realization_argv);

    try {
        miaow::parse_runtime_cli_options(
            static_cast<int>(runtime_argv.size()),
            runtime_argv.data(),
            runtime_options);
        realization_path = miaow::parse_realization_cli_path(
            static_cast<int>(realization_argv.size()),
            realization_argv.data(),
            "miaow_noah_owp",
            "realization.json");
        realization = miaow::read_realization_config(realization_path);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    std::cout << "Parsed realization with "
              << realization.subdomains_.size()
              << " subdomain(s)\n";

    // Register Noah-OWP BMI simulation unit and build one task per subdomain
    const std::string plugin_path = MIAOW_NOAH_OWP_PLUGIN_PATH;
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId noah_owp =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicFortranBmi>>(
            plugin_path);
    miaow::Runtime runtime(registry, runtime_options);

    std::vector<miaow::Task> tasks;
    tasks.reserve(realization.subdomains_.size());
    for (const miaow::RealizationSubdomain& subdomain : realization.subdomains_) {
        tasks.emplace_back(noah_owp, subdomain.config_path_);
    }

    std::cout << "Loaded Noah-OWP BMI plugin: " << plugin_path << "\n";

    // The bundled namelist writes output to ./output/output.nc
    std::error_code output_dir_error;
    std::filesystem::create_directories("output", output_dir_error);
    if (output_dir_error) {
        std::cerr << "Failed to create output directory './output': "
                  << output_dir_error.message() << "\n";
        return 1;
    }

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
