#pragma once

/**
 * Build a job for one statically linked C++ BMI heat model.
 */

#include <exception>
#include <utility>

#include "bmi_heat.hxx" // Statically linked BMI model

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_bmi.hpp" // MIAOW API for running BMI models as task executors

namespace miaow::examples::heat {

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry) {
    // Select the model initialization file
    caf::config_value::dictionary heat_config;
    heat_config.insert("init_config", "./data/configs/config_400x400.txt");

    // Register the statically linked heat model and create the runtime
    const miaow::TaskExecutorId heat =
        registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();

    // Queue one complete model run on the heat unit
    miaow::Job job;
    const miaow::SimulationUnitId unit = job.add_unit("heat", heat);
    job.add_task(unit, "run", heat_config);

    return job;
}

} // namespace miaow::examples::heat
