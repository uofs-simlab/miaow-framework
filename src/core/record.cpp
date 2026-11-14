#include "internal.hpp"
#include "core/build_info.hpp"
#include "miaow_io.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace miaow {

JobRecord::JobRecord(std::uint64_t run_id, JobId job_id,
                     std::vector<std::optional<TaskResult>> task_results,
                     std::vector<TaskTiming> task_timings,
                     std::chrono::duration<double> run_time, caf::config_value output)
    : run_id_{run_id}, job_id_{job_id}, task_results_{std::move(task_results)},
      task_timings_{std::move(task_timings)}, run_time_{run_time},
      output_{std::move(output)}, miaow_version_{core::build_version} {}

void Job::write_job_record(const JobRecord& record) const {
    const std::filesystem::path directory =
        core::job_directory_path(runtime_options_, run_id_, job_id_);
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "job_record.json";

    // Index operation names locally while task definitions remain available
    std::vector<std::string_view> operations;
    std::unordered_map<std::string_view, std::size_t> operation_ids;
    for (const Task& task : tasks_) {
        if (operation_ids.emplace(task.operation_, operations.size()).second) {
            operations.push_back(task.operation_);
        }
    }

    std::size_t failed_task_count = 0;
    for (const std::optional<TaskResult>& result : record.task_results_) {
        if (result->status_ == TaskStatus::failed) {
            ++failed_task_count;
        }
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" + path.string() + "'");
    }
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "{\n  \"run_id\": " << nlohmann::ordered_json(record.run_id_).dump()
           << ",\n  \"job_id\": " << nlohmann::ordered_json(record.job_id_).dump()
           << ",\n  \"miaow_version\": "
           << nlohmann::ordered_json(record.miaow_version_).dump() << ",\n  \"summary\": "
           << nlohmann::ordered_json{{"task_count", record.task_results_.size()},
                                     {"failed_task_count", failed_task_count},
                                     {"run_time_seconds", record.run_time_.count()}}
                  .dump();

    // Export controller observations as plain JSON
    output << ",\n  \"output\": " << to_json(record.output_);

    // Failure and artifact rows are sparse and derived from retained results
    output << ",\n  \"failures\": [";
    bool first = true;
    for (std::size_t index = 0; index < record.task_results_.size(); ++index) {
        const TaskResult& result = *record.task_results_[index];
        if (result.status_ == TaskStatus::failed) {
            output << (first ? "\n" : ",\n")
                   << nlohmann::ordered_json{{"task_id", index},
                                             {"message", result.message_}}
                          .dump();
            first = false;
        }
    }
    output << "\n  ],\n  \"artifacts\": [";
    first = true;
    for (std::size_t index = 0; index < record.task_results_.size(); ++index) {
        const TaskResult& result = *record.task_results_[index];
        if (!result.artifact_paths_.empty()) {
            output << (first ? "\n" : ",\n")
                   << nlohmann::ordered_json{{"task_id", index},
                                             {"paths", result.artifact_paths_}}
                          .dump();
            first = false;
        }
    }

    // Store labels once per unit, including units whose execution resources are gone
    output << "\n  ],\n  \"profile\": {\n    \"units\": [";
    for (std::size_t index = 0; index < units_.size(); ++index) {
        const UnitBinding& unit = units_[index];
        output << (index == 0 ? "\n" : ",\n")
               << nlohmann::ordered_json{{"unit_id", index},
                                         {"label", unit.feature_id_},
                                         {"task_executor_id", unit.task_executor_id_}}
                      .dump();
    }
    output << "\n    ],\n    \"operations\": [";
    for (std::size_t index = 0; index < operations.size(); ++index) {
        output << (index == 0 ? "" : ", ")
               << nlohmann::ordered_json(operations[index]).dump();
    }
    output << "],\n    \"columns\": [\"task_id\", \"unit_id\", \"operation_id\", "
              "\"status\", \"dispatch_offset_ns\", \"completion_offset_ns\"],"
              "\n    \"tasks\": [";
    for (std::size_t index = 0; index < tasks_.size(); ++index) {
        const Task& task = tasks_[index];
        const TaskTiming& timing = record.task_timings_[index];
        const auto row = nlohmann::ordered_json::array(
            {index, task.unit_id_, operation_ids.at(task.operation_),
             task_status_to_string(record.task_results_[index]->status_),
             timing.dispatch_offset_ns_ >= 0
                 ? nlohmann::ordered_json(timing.dispatch_offset_ns_)
                 : nullptr,
             timing.completion_offset_ns_ >= 0
                 ? nlohmann::ordered_json(timing.completion_offset_ns_)
                 : nullptr});
        output << (index == 0 ? "\n" : ",\n") << row.dump();
    }
    output << "\n    ]\n  }\n}\n";
    output.close();
}

} // namespace miaow
