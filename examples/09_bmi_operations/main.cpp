#include <exception>
#include <iomanip>
#include <iostream>
#include <utility>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "bmi_operations.hpp"

int main(int argc, char* argv[]) {
    try {
        // Resolve the shared heat configuration relative to the example directory
        if (!miaow::ensure_expected_run_dir(MIAOW_BMI_OPERATIONS_RUN_DIR)) {
            return 1;
        }
        miaow::RuntimeOptions runtime_options;
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);

        miaow::TaskExecutorRegistry registry;
        miaow::Job job = miaow::examples::bmi_operations::make_job(registry);
        miaow::Runtime runtime(registry, runtime_options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord job_record = job_handle.get();

        miaow::print_job_summary(job_record);
        if (!miaow::all_tasks_succeeded(job_record)) {
            return 1;
        }

        // Query results show that the model clock persists across update tasks
        std::cout << "\nModel times (seconds):" << std::setprecision(10);
        for (const caf::config_value& time :
             caf::get<caf::config_value::list>(job_record.output_)) {
            std::cout << " " << caf::get<double>(time);
        }
        std::cout << "\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
