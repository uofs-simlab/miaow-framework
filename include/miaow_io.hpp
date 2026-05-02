#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <caf/config_value.hpp>

#include "miaow.hpp"

namespace miaow {

/**
 * @brief Print diagnostics to stdout.
 */
void print_diagnostics(const JobDiagnostics& diagnostics, JobId job_id = 0);

/**
 * @brief Print all task results in a job to stdout.
 */
void print_task_results(const JobResult& job_result);

/**
 * @brief Return true when every task result in a job succeeded.
 */
bool all_tasks_succeeded(const JobResult& job_result);

/**
 * @brief Validate that an example is running from its required working directory.
 *
 * On mismatch, prints an actionable error message with the expected and current
 * directories and a suggested `cd` command.
 *
 * @return true when the current directory matches `expected_run_dir`, false otherwise.
 */
bool ensure_expected_run_dir(const std::string& executable_name,
                             const std::string& expected_run_dir);

/**
 * @brief Parse runtime-wide CLI arguments without workflow task inputs.
 *
 * Supported arguments:
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 * - `--output_root <path>`
 * - `--realization <path>` (used by realization-driven workflows; left empty
 *   when absent so callers may substitute their own default)
 *
 * @throws std::runtime_error if an argument is unrecognized, missing a value,
 * or has an invalid value.
 */
void parse_runtime_cli_options(int argc,
                               char* argv[],
                               RuntimeOptions& runtime_options);

/**
 * @brief Write a generated forcing CSV by repeating one hourly source cycle.
 *
 * The source CSV must define its timestamp in the first column, followed by
 * one or more data columns. The generated file is written under the system
 * temporary directory using `output_filename`.
 */
std::string write_repeated_hourly_forcing_csv(const std::string& source_path,
                                              const std::string& output_filename,
                                              std::size_t total_rows);

/**
 * @brief Build the structured forcing payload consumed by BMI tasks.
 *
 * `variables_names_map` is optional. When omitted, forcing falls back to
 * identity mapping between BMI input names and CSV headers.
 */
caf::config_value make_forcing_config(
    const std::string& forcing_file,
    const std::map<std::string, std::string>& variables_names_map = {});

/**
 * @brief One subdomain entry from a NextGen-style realization file.
 *
 * The current contract stores the subdomain identifier, a resolved model
 * config path, and optional MIAOW-managed forcing metadata.
 */
struct RealizationSubdomain {
    std::string subdomain_id_;
    std::string config_path_;
    std::optional<std::string> forcing_file_;
    std::map<std::string, std::string> variables_names_map_;
};

/**
 * @brief Parsed realization used to build one task per subdomain.
 *
 * The JSON shape follows the NextGen realization convention of a top-level
 * `subdomains` object keyed by subdomain ID. Each subdomain requires a
 * `config` string path and may optionally include `forcing_file` plus an
 * optional `variables_names_map`.
 */
struct RealizationConfig {
    std::string realization_path_;
    std::vector<RealizationSubdomain> subdomains_;
};

/**
 * @brief Read and validate a NextGen-style realization file.
 *
 * Relative subdomain input paths are resolved against the realization file's
 * parent directory. The parser fails if the JSON is malformed, `subdomains`
 * is missing or empty, any referenced path does not exist, or
 * `variables_names_map` is present without `forcing_file`.
 */
RealizationConfig read_realization_config(const std::string& realization_path);

} // namespace miaow
