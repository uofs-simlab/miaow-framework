#include <array>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "../config.hpp"
#include "model_coupling.hpp"

int main(int argc, char* argv[]) {
    try {
        // Resolve relative model inputs and common runtime options
        if (!miaow::ensure_expected_run_dir(MIAOW_MODEL_COUPLING_RUN_DIR)) {
            return 1;
        }
        miaow::RuntimeOptions options;
        const nlohmann::json config = read_example_config(argc, argv, options);
        std::vector<std::array<miaow::TaskId, 2>> temperature_queries;
        std::array<miaow::TaskId, 2> time_queries{};
        miaow::TaskExecutorRegistry registry;
        miaow::Job job =
            miaow::examples::model_coupling::make_job(registry, config, temperature_queries,
                                                      time_queries);
        miaow::Runtime runtime(registry, options);

        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
        const miaow::JobRecord record = job_handle.get();

        miaow::print_job_summary(record);
        if (!miaow::all_tasks_succeeded(record)) {
            return 1;
        }

        // Display recorded centre temperatures and final model clocks
        std::cout << "\nCentre temperatures (K) and final times (seconds):\n"
                  << std::setprecision(10);
        for (const std::array<miaow::TaskId, 2>& queries : temperature_queries) {
            for (std::size_t index = 0; index < queries.size(); ++index) {
                const auto& field = caf::get<caf::config_value::list>(
                    record.task_results_[queries[index]]->output_);
                std::cout << "  " << miaow::examples::model_coupling::unit_labels[index]
                          << ": " << caf::get<double>(field[12]) << " K\n";
            }
        }
        for (std::size_t index = 0; index < time_queries.size(); ++index) {
            const miaow::TaskResult& result = *record.task_results_[time_queries[index]];
            std::cout << "  " << miaow::examples::model_coupling::unit_labels[index] << ": "
                      << caf::get<double>(result.output_) << " s\n";
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
