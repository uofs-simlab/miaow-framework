/**
 * This example runs the CSDMS Fortran BMI heat model through MIAOW's dynamic
 * BMI plugin path. The model plugin is selected by explicit path and exports
 * the fixed register_bmi symbol expected by DynamicBmiModel.
 */

#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_cli.hpp"

namespace {

std::string parse_example_options(int argc,
                                  char* argv[],
                                  std::string& plugin_path,
                                  bool& expect_failure,
                                  std::vector<char*>& runtime_argv) {
    runtime_argv.clear();
    runtime_argv.push_back(argv[0]);

    int index = 1;
    while (index < argc) {
        const std::string option_name = argv[index];
        if (option_name == "--plugin_path") {
            if (index + 1 >= argc) {
                return "Error: --plugin_path requires a value";
            }
            plugin_path = argv[index + 1];
            index += 2;
            continue;
        }
        if (option_name == "--expect_failure") {
            expect_failure = true;
            ++index;
            continue;
        }

        // Forward all runtime options to MIAOW's shared runtime parser
        runtime_argv.push_back(argv[index]);
        ++index;
    }
    return "";
}

void print_job_result(const miaow::JobResult& job_result, bool& saw_failure) {
    for (std::size_t index = 0; index < job_result.task_results_.size(); ++index) {
        const miaow::TaskResult& task_result = job_result.task_results_.at(index);
        std::cout << "[" << task_result.task_id_ << "] "
                  << miaow::task_status_to_string(task_result.status_) << ": "
                  << task_result.message_ << "\n";
        if (task_result.status_ != miaow::TaskStatus::succeeded) {
            saw_failure = true;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::string plugin_path = MIAOW_FORTRAN_HEAT_PLUGIN_PATH;
    bool expect_failure = false;
    std::vector<char*> runtime_argv;
    const std::string example_error =
        parse_example_options(argc, argv, plugin_path, expect_failure, runtime_argv);
    if (!example_error.empty()) {
        std::cerr << example_error << "\n";
        return 1;
    }

    // Initialize runtime options after removing this example's plugin option
    miaow::RuntimeOptions runtime_options;
    const int runtime_argc = static_cast<int>(runtime_argv.size());
    const std::string input_error = miaow::parse_runtime_cli_options(
        runtime_argc,
        runtime_argv.data(),
        runtime_options);
    if (!input_error.empty()) {
        std::cerr << input_error << "\n";
        return 1;
    }

    // Register the dynamically loaded Fortran BMI model using the plugin path
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId heat =
        registry.add<miaow::BmiSimulationUnit<miaow::DynamicBmiModel>>(
            plugin_path);
    miaow::Runtime runtime(registry, runtime_options);

    const std::string heat_config = MIAOW_FORTRAN_HEAT_CONFIG_PATH;
    const std::vector<miaow::Task> tasks{
        {heat, heat_config},
        {heat, heat_config},
    };

    // Submit both tasks together to exercise independent model instances
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobResult job_result = job.get();

    bool saw_failure = false;
    print_job_result(job_result, saw_failure);
    miaow::print_diagnostics(job_result.diagnostics_);

    if (expect_failure) {
        return saw_failure ? 0 : 1;
    }
    return saw_failure ? 1 : 0;
}
