/** Run one SUMMA BMI task per GRU */
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    try {
        std::string config_path;
        miaow::RuntimeOptions options;
        options.job_request_timeout_ = std::chrono::minutes(20);

        // Consume the fixture path before passing common options to the runtime
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
        miaow::parse_runtime_cli_options(static_cast<int>(runtime_arguments.size()),
                                         runtime_arguments.data(), options);
        if (config_path.empty()) {
            throw std::runtime_error("--config requires a fixture manifest");
        }

        // The fixture manifest preserves attribute-file order
        std::ifstream input(config_path);
        if (!input) {
            throw std::runtime_error("Cannot open " + config_path);
        }

        // Parse the manifest JSON
        const auto manifest = nlohmann::json::parse(input);

        // Create the simulation unit registry and add the worker subprocess
        miaow::SimulationUnitRegistry registry;
        const auto worker = registry.add_subprocess(
            miaow::bmi_subprocess_executable_path(), miaow::SubprocessLifetime::single_task);

        // Create a task for each GRU in the manifest
        std::vector<miaow::Task> tasks;
        for (const auto& feature : manifest.at("grus")) {
            const auto identifier = feature.at("id").get<std::string>();
            miaow::DynamicBmiConfig config;
            config.backend_ = miaow::DynamicBmiBackend::fortran;
            config.plugin_path_ = MIAOW_SUMMA_PLUGIN_PATH;
            config.init_config_ = feature.at("init_config").get<std::string>();

            // SUMMA owns forcing, daily accumulation, and native NetCDF output
            tasks.push_back(miaow::make_bmi_task(
                worker, config, identifier,
                {{"feature_id", identifier}, {"feature_type", "gru"}}));
        }

        // Create the runtime, submit the tasks, and get the job record
        miaow::Runtime runtime(registry, options);
        const auto record = runtime.submit_job(tasks).get();

        // Print the job summary and return the overall success status
        miaow::print_job_summary(record);
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
