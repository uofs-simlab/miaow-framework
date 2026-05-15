#include <filesystem>
#include <string>

#include "io/internal.hpp"

namespace miaow::io {

void write_job_record(const RuntimeOptions& runtime_options, const JobRecord& job_record) {
    if (runtime_options.output_root_.empty()) {
        return;
    }

    const std::filesystem::path job_directory =
        std::filesystem::path(runtime_options.output_root_) /
        ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_));
    std::filesystem::create_directories(job_directory);

    write_json_file(job_directory / "job_record.json", job_record, false);
}

} // namespace miaow::io
