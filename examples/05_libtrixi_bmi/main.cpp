/**
 * This example runs libtrixi configs through MIAOW's dynamic BMI plugin path.
 * libtrixi calls these configs libelixirs.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

namespace {

constexpr int default_random_cfl_runs = 12;

std::vector<std::string> parse_cli_args(int argc, char* argv[],
                                        miaow::RuntimeOptions& runtime_options) {
    std::vector<std::string> runtime_arg_storage{argv[0]};
    std::vector<std::string> configs;

    // Split MIAOW runtime options from positional config paths
    for (int index = 1; index < argc;) {
        const std::string_view argument = argv[index];
        if (!argument.starts_with("--")) {
            configs.emplace_back(argument);
            ++index;
            continue;
        }

        runtime_arg_storage.emplace_back(argument);
        const bool option_has_value =
            argument == "--max_workers" || argument == "--task_request_timeout" ||
            argument == "--job_request_timeout" || argument == "--output_root" ||
            argument == "--realization";
        if (option_has_value && index + 1 < argc) {
            runtime_arg_storage.emplace_back(argv[index + 1]);
            index += 2;
            continue;
        }
        ++index;
    }

    std::vector<char*> runtime_argv;
    runtime_argv.reserve(runtime_arg_storage.size());
    for (std::string& runtime_arg : runtime_arg_storage) {
        runtime_argv.push_back(runtime_arg.data());
    }
    miaow::parse_runtime_cli_options(static_cast<int>(runtime_argv.size()),
                                     runtime_argv.data(), runtime_options);

    if (configs.empty()) {
        // Repeat the default config so each task samples its own CFL number
        for (int run = 0; run < default_random_cfl_runs; ++run) {
            configs.emplace_back(LIBTRIXI_CONFIG);
        }
    }
    return configs;
}

} // namespace

int main(int argc, char* argv[]) {
    // Check that local example data is resolved from the expected directory
    if (!miaow::ensure_expected_run_dir("miaow_libtrixi_bmi",
                                        MIAOW_LIBTRIXI_BMI_RUN_DIR)) {
        return 1;
    }

    // Parse runtime-wide CLI options and positional config paths
    miaow::RuntimeOptions runtime_options;
    std::vector<std::string> configs;
    try {
        configs = parse_cli_args(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    if (runtime_options.output_root_.empty()) {
        runtime_options.output_root_ = "./output";
    }

    // Seed subprocess-wide Julia defaults without overriding the user environment
    if (setenv("LIBTRIXI_PROJECT_DIR", LIBTRIXI_PROJECT_DIR, 0) != 0) {
        std::cerr << "failed to set libtrixi Julia environment\n";
        return 1;
    }
    if (!std::string_view(LIBTRIXI_JULIA_DEPOT_PATH).empty() &&
        setenv("JULIA_DEPOT_PATH", LIBTRIXI_JULIA_DEPOT_PATH, 0) != 0) {
        std::cerr << "failed to set libtrixi Julia environment\n";
        return 1;
    }
    if (!std::string_view(LIBTRIXI_JULIA_DEPOT_PATH).empty() &&
        setenv("LIBTRIXI_DEPOT_PATH", LIBTRIXI_JULIA_DEPOT_PATH, 0) != 0) {
        std::cerr << "failed to set libtrixi Julia environment\n";
        return 1;
    }

    // Get the plugin path from the compile-time constant
    const std::string plugin_path = LIBTRIXI_BMI_PLUGIN_PATH;
    std::cout << "libtrixi BMI plugin: " << plugin_path << "\n";

    // Register the pipeline unit that can be constructed inside the subprocess
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId pipeline =
        registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Build one BMI lifecycle task per config path
    std::vector<miaow::Task> tasks;
    tasks.reserve(configs.size());
    for (const std::string& config : configs) {
        tasks.push_back(miaow::make_dynamic_bmi_task(
            pipeline, "bmi_cpp_libtrixi", miaow::DynamicBmiBackend::cpp,
            plugin_path, config, {"simulation_time"},
            miaow::ExecutionMode::persistent_subprocess));
    }

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
