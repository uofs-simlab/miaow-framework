#include "miaow_io.hpp"

#include "io/internal.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace miaow {

namespace io {

std::string format_diagnostics(const JobRecord& job_record) {
    double sum_task_times = 0.0;
    for (const TaskRecord& task_record : job_record.tasks_) {
        sum_task_times += task_record.run_time_s_;
    }

    const double total_time = job_record.run_time_s_;
    const double concurrency = total_time > 0.0 ? (sum_task_times / total_time) : 0.0;

    std::ostringstream output;
    output << "\n=== MIAOW Job Diagnostics ===\n";
    output << "job_id: " << job_record.job_id_ << "\n";
    output << std::fixed << std::setprecision(6);
    output << "total_time: " << total_time << "\n";
    output << "concurrency: " << concurrency << "\n";
    output << "task_times:\n";
    for (const TaskRecord& task_record : job_record.tasks_) {
        output << "  [" << task_record.task_id_ << "] " << std::setw(10)
               << task_record.run_time_s_ << "\n";
    }
    output << "=============================\n";

    return output.str();
}

} // namespace io

void print_diagnostics(const JobRecord& job_record) {
    std::cout << io::format_diagnostics(job_record);
}

void print_task_results(const JobRecord& job_record) {
    for (const TaskRecord& task_record : job_record.tasks_) {
        std::cout << "[" << task_record.task_id_ << "] "
                  << task_status_to_string(task_record.result_.status_) << ": "
                  << task_record.result_.message_ << "\n";
    }
}

bool all_tasks_succeeded(const JobRecord& job_record) {
    for (const TaskRecord& task_record : job_record.tasks_) {
        if (task_record.result_.status_ != TaskStatus::succeeded) {
            return false;
        }
    }
    return true;
}

} // namespace miaow
