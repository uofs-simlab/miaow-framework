/** Run each SUMMA GRU as one complete BMI simulation in its own subprocess */
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

#include <nlohmann/json.hpp>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    try {
        std::string config_path;
        miaow::RuntimeOptions runtime_options;
        runtime_options.job_request_timeout_ = std::chrono::minutes(20);

        // Separate the GRU manifest option from the shared runtime options
        std::vector<char*> runtime_arguments{argv[0]};
        for (int index = 1; index < argc; ++index) {
            if (std::string(argv[index]) == "--config") {
                if (++index == argc) {
                    throw std::runtime_error("--config requires a manifest path");
                }
                config_path = argv[index];
            } else {
                runtime_arguments.push_back(argv[index]);
            }
        }

        // Parse the remaining runtime CLI options
        miaow::parse_runtime_cli_options(static_cast<int>(runtime_arguments.size()),
                                         runtime_arguments.data(), runtime_options);
        if (config_path.empty()) {
            throw std::runtime_error("--config requires a fixture manifest");
        }

        // Read GRU IDs and BMI input paths in manifest order
        std::ifstream manifest_file(config_path);
        if (!manifest_file) {
            throw std::runtime_error("Cannot open " + config_path);
        }

        const nlohmann::json manifest = nlohmann::json::parse(manifest_file);

        // Use separate subprocesses to isolate Fortran model state between GRUs
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId summa_executor =
            registry.add_subprocess(miaow::bmi_subprocess_executable_path());

        // Give each GRU a unit and queue its complete simulation
        miaow::Job job;
        for (const auto& feature : manifest.at("grus")) {
            const std::string gru_id = feature.at("id").get<std::string>();
            caf::config_value::dictionary config;
            config.insert("backend", "fortran");
            config.insert("plugin_path", MIAOW_SUMMA_PLUGIN_PATH);
            config.insert("init_config", feature.at("init_config").get<std::string>());

            // SUMMA handles its own forcing, daily accumulation, and NetCDF output
            const miaow::SimulationUnitHandle gru = job.add_unit(gru_id, summa_executor);
            miaow::Task task{gru, "run", config};
            task.metadata_["feature_type"] = "gru";
            job.add_task(std::move(task));
        }
        job.set_controller(std::make_unique<miaow::SingleTaskPerUnitController>());

        // Create the runtime using the requested concurrency and timeouts
        miaow::Runtime runtime(registry, runtime_options);

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord record = job_handle.get();

        // Print the job summary and return the overall success status
        miaow::print_job_summary(record);
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
