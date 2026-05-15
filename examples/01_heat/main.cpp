/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of
 * heat diffusion on a plate. The example uses the CSDMS BMI C++ heat example
 * implementation (`BmiHeat`) and accepts runtime CLI options.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "bmi_heat.hxx"  // defines the BmiHeat class that implements the BMI interface

#include "miaow.hpp"     // Core MIAOW runtime API
#include "miaow_bmi.hpp" // MIAOW API for running BMI models as simulation units
#include "miaow_io.hpp"  // I/O helpers for example programs

/**
 * @brief A simple simulation unit used to return a hello message. This is included to
 * demonstrate that multiple simulation units can be mixed in the same job.
 * In a real workflow, this could be replaced with other simulation units that perform
 * different tasks, such as data preprocessing, postprocessing, or running different models.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
public:
    miaow::TaskResult execute(const miaow::Task& task) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }
};

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir("miaow_heat", MIAOW_HEAT_RUN_DIR)) {
        return 1;
    }

    // Initialize runtime options and parse runtime-wide CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register simulation units and build runtime routing from the registry
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId hello = registry.add<HelloSimulationUnit>();
    const miaow::SimulationUnitId heat =
        registry.add<miaow::BmiSimulationUnit<BmiHeat>>();
    miaow::Runtime runtime(registry, runtime_options);

    // Get config file paths for this heat workload
    const std::string first_heat_config = "./data/configs/config_400x400.txt";
    const std::string second_heat_config = "./data/configs/config_800x800.txt";

    // Tasks for this job include heat simulations and one hello message
    std::vector<miaow::Task> tasks{
        {heat, miaow::make_bmi_model_pipeline_config(first_heat_config)},
        {heat, miaow::make_bmi_model_pipeline_config(second_heat_config)},
        {hello, "hello"},
    };

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
