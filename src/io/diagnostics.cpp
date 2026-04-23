#include "miaow_io.hpp"

#include "io/internal.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace miaow {

namespace io {

std::string format_diagnostics(const JobDiagnostics& diagnostics,
                               JobId job_id) {
    double sum_task_times = 0.0;
    for (double task_time : diagnostics.task_times_s_) {
        sum_task_times += task_time;
    }

    const double total_time = diagnostics.total_time_s_;
    const double concurrency = total_time > 0.0 ? (sum_task_times / total_time) : 0.0;

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

    return output.str();
}

std::string format_task_results(const JobResult& job_result) {
    std::ostringstream output;
    for (const TaskResult& task_result : job_result.task_results_) {
        output << "[" << task_result.task_id_ << "] "
               << task_status_to_string(task_result.status_) << ": "
               << task_result.message_ << "\n";
    }
    return output.str();
}

} // namespace io

void print_diagnostics(const JobDiagnostics& diagnostics, JobId job_id) {
    std::cout << io::format_diagnostics(diagnostics, job_id);
}

void print_task_results(const JobResult& job_result) {
    std::cout << io::format_task_results(job_result);
}

bool all_tasks_succeeded(const JobResult& job_result) {
    for (const TaskResult& task_result : job_result.task_results_) {
        if (task_result.status_ != TaskStatus::succeeded) {
            return false;
        }
    }
    return true;
}

} // namespace miaow
