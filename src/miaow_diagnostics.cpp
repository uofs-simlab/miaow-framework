#include <cstddef>
#include <iomanip>
#include <iostream>

#include "miaow_internal.hpp"

namespace miaow::internal {

void print_job_diagnostics(std::size_t job_id, const JobDiagnostics& diagnostics) {
    double sum_task_times = 0.0;
    for (const auto& task_duration : diagnostics.task_durations_s_) {
        sum_task_times += task_duration.count();
    }

    const double total_time = diagnostics.total_duration_s_.count();
    const double speedup = total_time > 0.0 ? (sum_task_times / total_time) : 0.0;

    std::cout << "\n=== MIAOW Job Diagnostics ===\n";
    std::cout << "job_id: " << job_id << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "total_time: " << total_time << "\n";
    std::cout << "speedup: " << speedup << "\n";
    std::cout << "task_times:\n";

    for (std::size_t task_id = 0; task_id < diagnostics.task_durations_s_.size(); ++task_id) {
        std::cout << "  [" << task_id << "] "
                  << std::setw(10) << diagnostics.task_durations_s_.at(task_id).count()
                  << "\n";
    }

    std::cout << "=============================\n";
}

} // namespace miaow::internal
