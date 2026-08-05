#include "miaow_io.hpp"

#include <chrono>
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

} // namespace

void print_job_summary(const JobRecord& job_record) {
    std::chrono::duration<double> sum_task_times{};
    for (const TaskRecord& task_record : job_record.tasks_) {
        sum_task_times += task_record.run_time_;
    }

    const std::chrono::duration<double> total_time = job_record.run_time_;
    const double concurrency = total_time > std::chrono::duration<double>::zero()
                                   ? sum_task_times / total_time
                                   : 0.0;

    std::ostringstream output;
    output << "\n" << section_header("MIAOW Job Summary") << "\n";
    output << "job_id: " << job_record.job_id_ << "\n";
    output << std::fixed << std::setprecision(6);
    output << "tasks:\n";
    for (const TaskRecord& task_record : job_record.tasks_) {
        output << "  [" << task_label(task_record) << "] "
               << task_status_to_string(task_record.result_.status_) << " ("
               << task_record.run_time_.count() << " s): "
               << task_record.result_.message_ << "\n";
    }
    output << "total_time: " << total_time.count() << "\n";
    output << "concurrency: " << concurrency << "\n";
    output << section_rule() << "\n";
    std::cout << output.str();
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
