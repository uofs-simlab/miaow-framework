#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "io/internal.hpp"

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

    // Build task metadata directly from the completed job result
    JobMetadata metadata;
    metadata.run_id_ = run_id;
    metadata.job_id_ = job_id;
    metadata.diagnostics_ = job_result.diagnostics_;
    metadata.tasks_.reserve(job_result.task_results_.size());
    for (const TaskResult& task_result : job_result.task_results_) {
        TaskRecord task_record;
        task_record.task_id_ = task_result.task_id_;
        task_record.simulation_unit_id_ = task_result.simulation_unit_id_;
        task_record.status_ = miaow::task_status_to_string(task_result.status_);
        task_record.message_ = task_result.message_;
        task_record.run_time_s_ = task_result.run_time_s_;
        task_record.artifact_paths_ = task_result.artifact_paths_;
        metadata.tasks_.push_back(std::move(task_record));
    }

    write_json_file(job_directory / "job_metadata.json", metadata, false);
}

} // namespace miaow::io
