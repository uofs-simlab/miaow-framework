#include "miaow_io.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace miaow {

namespace {

constexpr std::size_t section_width = 80;

std::string section_header(const std::string& title) {
    const std::string prefix = "=== " + title + " ";
    if (prefix.size() >= section_width) {
        return prefix.substr(0, section_width);
    }
    return prefix + std::string(section_width - prefix.size(), '=');
}

std::string section_rule() { return std::string(section_width, '='); }

std::string task_label(const TaskRecord& task_record) {
    if (!task_record.task_name_.empty()) {
        return task_record.task_name_;
    }
    return "task_" + std::to_string(task_record.task_id_);
}

std::string format_diagnostics(const JobRecord& job_record) {
    double sum_task_times = 0.0;
    for (const TaskRecord& task_record : job_record.tasks_) {
        sum_task_times += task_record.run_time_s_;
    }

    const double total_time = job_record.run_time_s_;
    const double concurrency = total_time > 0.0 ? (sum_task_times / total_time) : 0.0;

    std::ostringstream output;
    output << "\n" << section_header("MIAOW Job Diagnostics") << "\n";
    output << "job_id: " << job_record.job_id_ << "\n";
    output << std::fixed << std::setprecision(6);
    output << "total_time: " << total_time << "\n";
    output << "concurrency: " << concurrency << "\n";
    output << "task_times:\n";
    for (const TaskRecord& task_record : job_record.tasks_) {
        output << "  [" << task_label(task_record) << "] " << std::setw(10)
               << task_record.run_time_s_ << "\n";
    }
    output << section_rule() << "\n";

    return output.str();
}

} // namespace

void print_diagnostics(const JobRecord& job_record) {
    std::cout << format_diagnostics(job_record);
}

void print_task_results(const JobRecord& job_record) {
    std::cout << "\n" << section_header("MIAOW Task Results") << "\n";
    for (const TaskRecord& task_record : job_record.tasks_) {
        std::cout << "[" << task_label(task_record) << "] "
                  << task_status_to_string(task_record.result_.status_) << ": "
                  << task_record.result_.message_ << "\n";
    }
    std::cout << section_rule() << "\n";
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
