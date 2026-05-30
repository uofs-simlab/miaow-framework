#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "io/internal.hpp"

namespace miaow::io {

namespace json = boost::json;

namespace {

// Convert string vectors to JSON arrays
json::array string_vector_to_json(const std::vector<std::string>& values) {
    json::array array;
    array.reserve(values.size());
    for (const std::string& value : values) {
        array.emplace_back(json::value_from(value));
    }
    return array;
}

// Convert metadata maps to JSON objects
json::object metadata_to_json(const TaskMetadata& metadata) {
    json::object object;
    for (const std::pair<const std::string, std::string>& entry : metadata) {
        object.emplace(entry.first, json::value_from(entry.second));
    }
    return object;
}

// Convert task results to JSON objects
json::object task_result_to_json(const TaskResult& task_result) {
    json::object object;
    object.emplace("status", json::value_from(task_status_to_string(task_result.status_)));
    object.emplace("message", json::value_from(task_result.message_));
    object.emplace("artifact_paths", string_vector_to_json(task_result.artifact_paths_));
    return object;
}

// Convert task records to JSON objects
json::object task_record_to_json(const TaskRecord& task_record) {
    json::object object;
    object.emplace("task_id", task_record.task_id_);
    object.emplace("simulation_unit_id", task_record.simulation_unit_id_);
    object.emplace("metadata", metadata_to_json(task_record.metadata_));
    object.emplace("result", task_result_to_json(task_record.result_));
    object.emplace("run_time_s", task_record.run_time_s_);
    return object;
}

// Convert job records to JSON objects
json::object job_record_to_json(const JobRecord& job_record) {
    json::array task_records;
    task_records.reserve(job_record.tasks_.size());
    for (const TaskRecord& task_record : job_record.tasks_) {
        task_records.emplace_back(task_record_to_json(task_record));
    }

    json::object object;
    object.emplace("run_id", job_record.run_id_);
    object.emplace("job_id", job_record.job_id_);
    object.emplace("tasks", std::move(task_records));
    object.emplace("run_time_s", job_record.run_time_s_);
    return object;
}

} // namespace

void write_job_record(const RuntimeOptions& runtime_options, const JobRecord& job_record) {
    if (runtime_options.output_root_.empty()) {
        return;
    }

    const std::filesystem::path job_directory =
        std::filesystem::path(runtime_options.output_root_) /
        ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_));
    std::filesystem::create_directories(job_directory);

    const std::filesystem::path output_path = job_directory / "job_record.json";
    std::ofstream output(output_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" + output_path.string() + "'");
    }
    output << json::serialize(job_record_to_json(job_record)) << "\n";
}

} // namespace miaow::io
