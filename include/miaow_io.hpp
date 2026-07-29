#pragma once

#include <cstddef>
#include <map>
#include <string>

#include <caf/config_value.hpp>

#include "miaow.hpp"

namespace miaow {

/**
 * @brief Print task results and aggregate job timing information to stdout.
 * @param job_record `JobRecord` payload containing task results and timings.
 */
void print_job_summary(const JobRecord& job_record);

/**
 * @brief Return true when every task result in a job succeeded.
 * @param job_record `JobRecord` payload to inspect.
 * @return true when every task status is `TaskStatus::succeeded`.
 */
bool all_tasks_succeeded(const JobRecord& job_record);

/**
 * @brief Validate that an example is running from its required working directory.
 *
 * On mismatch, prints an actionable error message with the expected and current
 * directories and a suggested `cd` command.
 *
 * @param expected_run_dir Required current working directory.
 * @return true when the current directory matches `expected_run_dir`, false otherwise.
 * @throws std::filesystem::filesystem_error if the current working directory
 * cannot be resolved.
 */
bool ensure_expected_run_dir(const std::string& expected_run_dir);

/**
 * @brief Read model-independent runtime CLI arguments into `RuntimeOptions`.
 *
 * These arguments override the default `RuntimeOptions` values. The following
 * arguments are recognized:
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 * - `--output_root <path>`
 *
 * @param argc Number of CLI arguments.
 * @param argv CLI argument vector.
 * @param runtime_options `RuntimeOptions` updated from parsed arguments.
 * @throws std::runtime_error if an argument is unrecognized, missing a value,
 * or has an invalid value.
 */
void parse_runtime_cli_options(int argc, char* argv[], RuntimeOptions& runtime_options);

/**
 * @brief Write a generated forcing CSV by repeating one hourly source cycle.
 *
 * The source CSV must define its timestamp in the first column, followed by
 * one or more data columns. The generated file is written under the system
 * temporary directory using `output_filename`.
 *
 * @param source_path Source CSV path.
 * @param output_filename Filename used under the system temporary directory.
 * @param total_rows Number of rows to write to the generated forcing CSV.
 * @return Filesystem path to the generated forcing CSV.
 * @throws std::runtime_error if the source CSV is missing, malformed, or
 * cannot be written to the generated output path.
 * @throws std::filesystem::filesystem_error if the temporary output path
 * cannot be resolved.
 */
std::string write_repeated_hourly_forcing_csv(const std::string& source_path,
                                              const std::string& output_filename,
                                              std::size_t total_rows);

/**
 * @brief Build the structured forcing payload consumed by BMI tasks.
 *
 * `variables_names_map` is optional. When omitted, forcing falls back to
 * identity mapping between BMI input names and CSV headers.
 *
 * @param forcing_file Path to the forcing CSV file.
 * @param variables_names_map Mapping from BMI input variable names to CSV headers.
 * @return Structured `caf::config_value` forcing payload for BMI tasks.
 * @throws std::runtime_error if `forcing_file` or any explicit mapping entry
 * is empty.
 */
caf::config_value
make_forcing_config(const std::string& forcing_file,
                    const std::map<std::string, std::string>& variables_names_map = {});

} // namespace miaow
