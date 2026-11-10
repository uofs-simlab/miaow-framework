/**
 * Exchange complete temperature snapshots between two retained BMI heat models. This is 
 * done here by building a fixed DAG of tasks representing the coupling steps.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bmi_heat.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"

namespace {

// Build the complete coupling DAG for one source and one receiver heat model
miaow::Job make_coupling_job(miaow::TaskExecutorId heat, std::int64_t number_of_steps) {
    // Declare the two model units
    miaow::Job job;
    const std::array units{job.add_unit("source", heat), job.add_unit("receiver", heat)};

    // Initialize both models using the shared heat configuration
    const std::string temperature = "plate_surface__temperature";
    caf::config_value::dictionary model_config;
    model_config.insert("init_config", "data/configs/heat.txt");
    std::array<miaow::TaskHandle, 2> initialization;
    for (std::size_t index = 0; index < units.size(); ++index) {
        initialization[index] = job.add_task(units[index], "initialize", model_config);
    }

    // Seed the source with a central 1 K pulse on its 5 by 5 grid
    caf::config_value::list initial_field(25, caf::config_value{0.0});
    initial_field[12] = caf::config_value{1.0};

    // Set the initial temperature field on the source model and previous task handles
    miaow::TaskHandle previous_source_task =
        job.add_task(units[0], initialization[0], "set_value", temperature,
                     std::move(initial_field));
    miaow::TaskHandle previous_receiver_task = initialization[1];

    // Build the coupling time steps
    for (std::int64_t step = 0; step < number_of_steps; ++step) {
        // Advance the source and capture its temperature field
        const miaow::TaskHandle source_update =
            job.add_task(units[0], previous_source_task, "update");
        const miaow::TaskHandle source_get_value =
            job.add_task(units[0], source_update, "get_value", temperature);

        // Transfer after both the source read and receiver's preceding work finish
        const miaow::TaskHandle receiver_set_value =
            job.add_task(units[1],
                         std::vector<miaow::TaskHandle>{previous_receiver_task,
                                                       source_get_value},
                         "set_value", temperature, miaow::output_of(source_get_value));

        // Advance the receiver using the transferred field
        const miaow::TaskHandle receiver_update =
            job.add_task(units[1], receiver_set_value, "update");

        // Capture the receiver field before its next write
        previous_receiver_task =
            job.add_task(units[1], receiver_update, "get_value", temperature);

        // The source can advance again as soon as its snapshot has been captured
        previous_source_task = source_get_value;
    }

    // Query final model times before explicitly finalizing each model
    const std::array previous_tasks{previous_source_task, previous_receiver_task};
    for (std::size_t index = 0; index < units.size(); ++index) {
        const miaow::TaskHandle get_current_time =
            job.add_task(units[index], previous_tasks[index], "get_current_time");
        job.add_task(units[index], get_current_time, "finalize");
    }

    return job;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        // Resolve relative model inputs and common runtime options
        if (!miaow::ensure_expected_run_dir(MIAOW_MODEL_COUPLING_RUN_DIR)) {
            return 1;
        }
        miaow::RuntimeOptions options;
        const nlohmann::json config = read_example_config(argc, argv, options);
        const std::int64_t number_of_steps =
            config.value("number_of_steps", std::int64_t{3});
        if (number_of_steps < 0) {
            throw std::invalid_argument("number_of_steps must be non-negative");
        }

        // Register the heat model; each unit retains its own executor and model
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId heat =
            registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
        miaow::Runtime runtime(registry, options);

        // Construct the coupling DAG
        miaow::Job job = make_coupling_job(heat, number_of_steps);

        // Execute the graph and report any task failures
        const miaow::JobRecord record = runtime.submit_job(std::move(job)).get();
        miaow::print_job_summary(record);
        if (!miaow::all_tasks_succeeded(record)) {
            return 1;
        }

        // Display recorded centre temperatures and final model clocks
        std::cout << "\nCentre temperatures (K) and final times (seconds):\n"
                  << std::setprecision(10);
        for (const miaow::TaskRecord& task : record.tasks_) {
            if (task.operation_ == "get_value") {
                const auto& field = caf::get<caf::config_value::list>(task.result_.output_);
                std::cout << "  " << task.metadata_.at("feature_id") << ": "
                          << caf::get<double>(field[12]) << " K\n";
            } else if (task.operation_ == "get_current_time") {
                std::cout << "  " << task.metadata_.at("feature_id") << ": "
                          << caf::get<double>(task.result_.output_) << " s\n";
            }
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
