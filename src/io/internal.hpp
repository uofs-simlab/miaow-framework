#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "miaow.hpp"

namespace miaow::io {

std::string trim(std::string value);

std::vector<std::string> split_csv_line(const std::string& line);

std::vector<std::string> read_csv_header(const std::string& csv_path);

std::size_t find_csv_column_index(const std::vector<std::string>& headers,
                                  const std::string& column_name);

std::vector<double> read_csv_numeric_column(const std::string& csv_path,
                                            const std::string& column_name);

std::string format_diagnostics(const JobDiagnostics& diagnostics, JobId job_id = 0);

std::string format_task_results(const JobResult& job_result);

void write_job_metadata(std::uint64_t run_id,
                        JobId job_id,
                        const RuntimeOptions& runtime_options,
                        const JobResult& job_result);

} // namespace miaow::io
