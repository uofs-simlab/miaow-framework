#pragma once

/**
 * Exchange complete temperature snapshots between two retained BMI heat models. This is
 * done here by building a fixed DAG of tasks representing the coupling steps.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bmi_heat.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include <nlohmann/json.hpp>

namespace miaow::examples::model_coupling {

constexpr std::array unit_labels{"source", "receiver"};

// Build the complete coupling DAG for one source and one receiver heat model
inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry,
                           const nlohmann::json& config,
                           std::vector<std::array<miaow::TaskId, 2>>& temperature_queries,
                           std::array<miaow::TaskId, 2>& time_queries) {
    const std::int64_t number_of_steps = config.value("number_of_steps", std::int64_t{3});
    if (number_of_steps < 0) {
        throw std::invalid_argument("number_of_steps must be non-negative");
    }

    // Register the heat model; each unit retains its own executor and model
    const miaow::TaskExecutorId heat =
        registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();

    // Declare the two model units
    miaow::Job job;
    const std::array units{job.add_unit(unit_labels[0], heat),
                           job.add_unit(unit_labels[1], heat)};

    // Initialize both models using the shared heat configuration
    const std::string temperature = "plate_surface__temperature";
    caf::config_value::dictionary model_config;
    model_config.insert("init_config", "data/configs/heat.txt");
    std::array<miaow::TaskId, 2> initialization{};
    for (std::size_t index = 0; index < units.size(); ++index) {
        initialization[index] = job.add_task(units[index], "initialize", model_config);
    }

    // Seed the source with a central 1 K pulse on its 5 by 5 grid
    caf::config_value::list initial_field(25, caf::config_value{0.0});
    initial_field[12] = caf::config_value{1.0};

    // Set the initial temperature field on the source model and previous task IDs
    miaow::TaskId previous_source_task =
        job.add_task(units[0], initialization[0], "set_value", temperature,
                     std::move(initial_field));
    miaow::TaskId previous_receiver_task = initialization[1];

    // Build the coupling time steps
    for (std::int64_t step = 0; step < number_of_steps; ++step) {
        // Advance the source and capture its temperature field
        const miaow::TaskId source_update =
            job.add_task(units[0], previous_source_task, "update");
        const miaow::TaskId source_get_value =
            job.add_task(units[0], source_update, "get_value", temperature);

        // Transfer after both the source read and receiver's preceding work finish
        const miaow::TaskId receiver_set_value =
            job.add_task(units[1],
                         std::vector<miaow::TaskId>{previous_receiver_task,
                                                    source_get_value},
                         "set_value", temperature, miaow::output_of(source_get_value));

        // Advance the receiver using the transferred field
        const miaow::TaskId receiver_update =
            job.add_task(units[1], receiver_set_value, "update");

        // Capture the receiver field before its next write
        previous_receiver_task =
            job.add_task(units[1], receiver_update, "get_value", temperature);
        temperature_queries.push_back({source_get_value, previous_receiver_task});

        // The source can advance again as soon as its snapshot has been captured
        previous_source_task = source_get_value;
    }

    // Query final model times before explicitly finalizing each model
    const std::array previous_tasks{previous_source_task, previous_receiver_task};
    for (std::size_t index = 0; index < units.size(); ++index) {
        const miaow::TaskId get_current_time =
            job.add_task(units[index], previous_tasks[index], "get_current_time");
        time_queries[index] = get_current_time;
        job.add_task(units[index], get_current_time, "finalize");
    }

    return job;
}

} // namespace miaow::examples::model_coupling
