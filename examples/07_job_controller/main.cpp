#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"
#include "job_controller.hpp"

int main(int argc, char* argv[]) {
    // Require execution from the example source directory so relative input paths resolve
    if (!miaow::ensure_expected_run_dir(MIAOW_JOB_CONTROLLER_RUN_DIR)) {
        return 1;
    }

    miaow::RuntimeOptions runtime_options;
    try {
        // Resolve example inputs and defaults before constructing execution objects
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);
        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::examples::controller::make_job(registry, config);
        miaow::Runtime runtime(registry, runtime_options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord job_record = job_handle.get();

        // Report task failures before displaying the converged iteration sequence
        miaow::print_job_summary(job_record);
        if (!miaow::all_tasks_succeeded(job_record)) {
            return 1;
        }

        // Print the structured output recorded for every completed iteration
        std::cout << "\nHeron iterations:\n" << std::setprecision(10);
        for (std::size_t index = 0; index < job_record.task_results_.size(); ++index) {
            const caf::config_value::dictionary& output =
                caf::get<caf::config_value::dictionary>(
                    job_record.task_results_[index]->output_);
            std::cout << "  iteration " << index + 1 << ": estimate="
                      << caf::get<caf::config_value::real>(output, "estimate")
                      << ", residual="
                      << caf::get<caf::config_value::real>(output, "residual") << "\n";
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
