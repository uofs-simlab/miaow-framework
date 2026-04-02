/**
 * This example demonstrates how to orchestrate the vendored SoilFreezeThaw BMI model
 * with MIAOW using one or more configuration files.
 */

#include <iostream>
#include <string>
#include <vector>

#include "bmi_soil_freeze_thaw.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_cli.hpp"

static std::string task_status_string(miaow::TaskStatus status) {
    if (status == miaow::TaskStatus::succeeded) {
        return "succeeded";
    }
    return "failed";
}

int main(int argc, char* argv[]) {
    miaow::RuntimeOptions runtime_options;
    std::vector<std::string> config_files;

    const std::string input_error = miaow::parse_and_validate_workflow_cli_options(
        argc, argv, runtime_options, config_files);
    if (!input_error.empty()) {
        std::cerr << input_error << "\n"
                  << miaow::workflow_cli_usage("miaow_soil_freeze_thaw") << "\n";
        return 1;
    }

    miaow::SimulationUnitRegistry simulation_unit_registry;
    const miaow::SimulationUnitId soil_freeze_thaw =
        simulation_unit_registry.add<miaow::BmiSimulationUnit<BmiSoilFreezeThaw>>();

    miaow::Runtime runtime(simulation_unit_registry, runtime_options);

    std::vector<miaow::Task> tasks;
    tasks.reserve(config_files.size());
    for (const std::string& config_file : config_files) {
        tasks.emplace_back(soil_freeze_thaw, config_file);
    }

    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobResult job_result = job.get();

    for (std::size_t index = 0; index < job_result.task_results_.size(); ++index) {
        const miaow::TaskResult& task_result = job_result.task_results_.at(index);
        std::cout << "[" << index << "] "
                  << task_status_string(task_result.status_) << ": "
                  << task_result.message_ << "\n";
    }

    miaow::print_diagnostics(job_result.diagnostics_);

    return 0;
}
