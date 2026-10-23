/**
 * 06_fuse_bmi: runs a Saltelli batch of FUSE hydrologic model simulations
 * through MIAOW's dynamic Fortran BMI plugin path.
 *
 * Configure, build, and run from the repository root:
 *
 *   cmake -S . -B build -DBUILD_EXAMPLES=ON \
 *       -DMIAOW_EXAMPLES=06_fuse_bmi -DMIAOW_FUSE_FETCH_DATA=ON
 *   cmake --build build --target miaow_fuse_bmi
 *   cd examples/06_fuse_bmi
 *   ./miaow_fuse_bmi --max_workers 6 --task_request_timeout 900 \
 *       --job_request_timeout 14400
 *
 * CMake resolves the FUSE grid data, uses the tracked parameter sets, and
 * creates one isolated task configuration per sample.
 */
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {

    // Initialize runtime options and parse CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Path to the compiled FUSE BMI shared library plugin
    const std::string fuse_plugin_path = MIAOW_FUSE_PLUGIN_PATH;
    std::cout << "FUSE BMI plugin: " << fuse_plugin_path << "\n";

    // Directory containing per-parameter-set BMI config files
    // Each file is named fuse_bmi_config_NNNNN.txt and contains:
    //   line 1: path to FUSE file manager
    //   line 2: domain ID
    //   line 3: parameter set NetCDF filename
    //   line 4: FUSE output mode
    const std::string config_dir = MIAOW_FUSE_CONFIG_DIR;
    std::cout << "Config directory: " << config_dir << "\n";

    // Discover all config files in the config directory
    std::vector<std::string> config_files;
    for (const auto& entry : std::filesystem::directory_iterator(config_dir)) {
        if (entry.path().extension() == ".txt") {
            config_files.push_back(entry.path().string());
        }
    }
    std::sort(config_files.begin(), config_files.end());
    std::cout << "Found " << config_files.size() << " FUSE parameter set configs\n\n";

    if (config_files.empty()) {
        std::cerr << "No config files found in " << config_dir << "\n";
        return 1;
    }

    // Register the bundled BMI worker for one FUSE task per subprocess
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId bmi_subprocess =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path(),
                                miaow::SubprocessLifetime::single_task);

    // Create the runtime with the registered simulation unit and options
    miaow::Runtime runtime(registry, runtime_options);

    // Create one DynamicBmiConfig per parameter set
    std::vector<miaow::DynamicBmiConfig> configs;
    for (const auto& config_file : config_files) {
        configs.push_back({
            .backend_ = miaow::DynamicBmiBackend::fortran,
            .plugin_path_ = fuse_plugin_path,
            .init_config_ = config_file,
        });
    }

    // Build task list and submit job
    const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(bmi_subprocess, configs);
    std::cout << "Submitting " << tasks.size() << " FUSE tasks to MIAOW...\n\n";
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print results and diagnostics
    miaow::print_job_summary(job_record);

    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
