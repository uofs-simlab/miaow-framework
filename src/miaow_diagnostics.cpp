#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "miaow.hpp"

namespace miaow {

std::string format_diagnostics(const JobDiagnostics& diagnostics,
                               std::size_t job_id) {
    // Add up task times
    double sum_task_times = 0.0;
    for (double task_time : diagnostics.task_times_s_) {
        sum_task_times += task_time;
    }

    // Define concurrency as the sum of task times divided by total time
    const double total_time = diagnostics.total_time_s_;
    const double concurrency = total_time > 0.0 ? (sum_task_times / total_time) : 0.0;

    // Format diagnostics into a human-readable block
    std::ostringstream output;
    output << "\n=== MIAOW Job Diagnostics ===\n";
    output << "job_id: " << job_id << "\n";
    output << std::fixed << std::setprecision(6);
    output << "total_time: " << total_time << "\n";
    output << "concurrency: " << concurrency << "\n";
    output << "task_times:\n";
    for (std::size_t task_id = 0; task_id < diagnostics.task_times_s_.size(); ++task_id) {
        output << "  [" << task_id << "] "
               << std::setw(10) << diagnostics.task_times_s_.at(task_id)
               << "\n";
    }
    output << "=============================\n";

    // Return the formatted diagnostics string
    return output.str();
}

// Print diagnostics to stdout
void print_diagnostics(const JobDiagnostics& diagnostics, std::size_t job_id) {
    std::cout << format_diagnostics(diagnostics, job_id);
}

} // namespace miaow
