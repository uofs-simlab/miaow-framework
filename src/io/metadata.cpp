#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <caf/json.hpp>

#include "metadata.hpp"

namespace miaow::io {

namespace {

struct TaskRecord {
    TaskId task_id_{0};
    SimulationUnitId simulation_unit_id_{0};
    std::string status_;
    std::string message_;
    double run_time_s_{0.0};
    std::vector<std::string> artifact_paths_;
};

struct JobMetadata {
    std::uint64_t run_id_{0};
    JobId job_id_{0};
    JobDiagnostics diagnostics_;
    std::vector<TaskRecord> tasks_;
};

template <class Inspector>
bool inspect(Inspector& f, TaskRecord& x) {
    return f.object(x).fields(f.field("task_id", x.task_id_),
                              f.field("simulation_unit_id", x.simulation_unit_id_),
                              f.field("status", x.status_),
                              f.field("message", x.message_),
                              f.field("run_time_s", x.run_time_s_),
                              f.field("artifact_paths", x.artifact_paths_));
}

template <class Inspector>
bool inspect(Inspector& f, JobMetadata& x) {
    return f.object(x).fields(f.field("run_id", x.run_id_),
                              f.field("job_id", x.job_id_),
                              f.field("diagnostics", x.diagnostics_),
                              f.field("tasks", x.tasks_));
}

TaskRecord make_task_record(const TaskResult& task_result) {
    TaskRecord task_record;
    task_record.task_id_ = task_result.task_id_;
    task_record.simulation_unit_id_ = task_result.simulation_unit_id_;
    task_record.status_ = miaow::task_status_to_string(task_result.status_);
    task_record.message_ = task_result.message_;
    task_record.run_time_s_ = task_result.run_time_s_;
    task_record.artifact_paths_ = task_result.artifact_paths_;
    return task_record;
}

JobMetadata make_job_metadata(std::uint64_t run_id,
                              JobId job_id,
                              const JobResult& job_result) {
    JobMetadata job_metadata;
    job_metadata.run_id_ = run_id;
    job_metadata.job_id_ = job_id;
    job_metadata.diagnostics_ = job_result.diagnostics_;
    job_metadata.tasks_.reserve(job_result.task_results_.size());

    for (const TaskResult& task_result : job_result.task_results_) {
        job_metadata.tasks_.push_back(make_task_record(task_result));
    }
    return job_metadata;
}

} // namespace

void write_job_metadata(std::uint64_t run_id,
                        JobId job_id,
                        const RuntimeOptions& runtime_options,
                        const JobResult& job_result) {
    if (runtime_options.output_root_.empty()) {
        return;
    }

    const std::filesystem::path job_directory =
        std::filesystem::path(runtime_options.output_root_) /
        ("run_" + std::to_string(run_id)) /
        ("job_" + std::to_string(job_id));
    std::filesystem::create_directories(job_directory);

    std::ofstream metadata_file(job_directory / "job_metadata.json");
    if (!metadata_file.is_open()) {
        throw std::ios_base::failure("Failed to open job_metadata.json for writing");
    }
    metadata_file.exceptions(std::ios::failbit | std::ios::badbit);

    JobMetadata metadata = make_job_metadata(run_id, job_id, job_result);

    caf::json_builder builder;
    builder.skip_object_type_annotation(true);
    builder.skip_empty_fields(false);
    if (!builder.apply(metadata)) {
        throw std::runtime_error("Failed to serialize job metadata: " +
                                 caf::to_string(builder.get_error()));
    }

    const caf::json_value json_value = builder.seal();
    std::string json_text;
    json_value.print_to(json_text, 2);
    metadata_file << json_text << "\n";
    metadata_file.flush();
}

} // namespace miaow::io
