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

std::string section_rule() {
    return std::string(section_width, '=');
}

} // namespace

void print_job_summary(const JobRecord& job_record) {
    std::chrono::duration<double> sum_task_times{};
    for (const TaskTiming& timing : job_record.task_timings_) {
        if (timing.dispatch_offset_ns_ >= 0 && timing.completion_offset_ns_ >= 0) {
            sum_task_times += std::chrono::nanoseconds{timing.completion_offset_ns_ -
                                                       timing.dispatch_offset_ns_};
        }
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
    for (std::size_t index = 0; index < job_record.task_results_.size(); ++index) {
        const TaskResult& result = *job_record.task_results_[index];
        const TaskTiming& timing = job_record.task_timings_[index];
        const std::chrono::duration<double> run_time =
            timing.dispatch_offset_ns_ >= 0 && timing.completion_offset_ns_ >= 0
                ? std::chrono::nanoseconds{timing.completion_offset_ns_ -
                                           timing.dispatch_offset_ns_}
                : std::chrono::nanoseconds::zero();
        output << "  [task_" << index << "] " << task_status_to_string(result.status_)
               << " (" << run_time.count() << " s)";
        if (!result.message_.empty()) {
            output << ": " << result.message_;
        }
        output << "\n";
    }
    output << "total_time: " << total_time.count() << "\n";
    output << "concurrency: " << concurrency << "\n";
    output << section_rule() << "\n";
    std::cout << output.str();
}

bool all_tasks_succeeded(const JobRecord& job_record) {
    for (const std::optional<TaskResult>& result : job_record.task_results_) {
        if (result->status_ != TaskStatus::succeeded) {
            return false;
        }
    }
    return true;
}

} // namespace miaow
