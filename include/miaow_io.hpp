#pragma once

#include <string>

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

} // namespace miaow
