#pragma once

#include <string>
#include <vector>

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
 * @brief Parse runtime-wide CLI arguments without workflow task inputs.
 *
 * Supported arguments:
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 * - `--output_root <path>`
 *
 * @throws std::runtime_error if an argument is unrecognized, missing a value,
 * or has an invalid value.
 */
void parse_runtime_cli_options(int argc,
                               char* argv[],
                               RuntimeOptions& runtime_options);

/**
 * @brief Parse CLI arguments for a realization-driven example.
 *
 * Supported arguments:
 * - `--realization <path>`
 *
 * `--help` prints usage for the supplied executable name and exits with success.
 * The default realization path is returned when no arguments are supplied.
 *
 * @throws std::runtime_error if an argument is unrecognized or missing a value.
 */
std::string parse_realization_cli_path(int argc,
                                       char* argv[],
                                       const std::string& executable_name,
                                       const std::string& default_realization_path);

/**
 * @brief One subdomain entry from a NextGen-style realization file.
 *
 * The current contract stores the subdomain identifier and a resolved model
 * config path. Forcing remains model-owned, matching the config-only
 * orchestration path used by the Noah-OWP example.
 */
struct RealizationSubdomain {
    std::string subdomain_id_;
    std::string config_path_;
};

/**
 * @brief Parsed config-only realization used to build one task per subdomain.
 *
 * The JSON shape follows the NextGen realization convention of a top-level
 * `subdomains` object keyed by subdomain ID. Each subdomain currently requires
 * a `config` string path.
 */
struct RealizationConfig {
    std::string realization_path_;
    std::vector<RealizationSubdomain> subdomains_;
};

/**
 * @brief Read and validate a config-only NextGen-style realization file.
 *
 * Relative subdomain config paths are resolved against the realization file's
 * parent directory. The parser fails if the JSON is malformed, `subdomains` is
 * missing or empty, or any referenced config path does not exist.
 */
RealizationConfig read_realization_config(const std::string& realization_path);

} // namespace miaow
