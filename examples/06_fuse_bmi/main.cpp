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
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    try {
        // Initialize runtime options and parse CLI arguments
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

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

        // Sort parameter-set files so task IDs follow a reproducible input order
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

        // Register the bundled BMI executable; each parameter-set unit owns its child
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId bmi_subprocess =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path());

        // Create the runtime with the registered task executor and options
        miaow::Runtime runtime(registry, runtime_options);

        // Prepare one unit and one complete model run per parameter set
        miaow::Job job;
        for (const std::string& config_file : config_files) {
            caf::config_value::dictionary config;
            config.insert("backend", "fortran");
            config.insert("plugin_path", fuse_plugin_path);
            config.insert("init_config", config_file);
            const miaow::SimulationUnitId unit =
                job.add_unit(std::filesystem::path(config_file).stem().string(),
                             bmi_subprocess);
            job.add_task(unit, "run", config);
        }
        job.set_controller(std::make_unique<miaow::SingleTaskPerUnitController>());

        // Report the number of model runs
        std::cout << "Submitting " << config_files.size() << " FUSE tasks to MIAOW...\n\n";

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord job_record = job_handle.get();

        // Print results and diagnostics
        miaow::print_job_summary(job_record);

        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
