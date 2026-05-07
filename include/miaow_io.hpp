#pragma once

#include <cstddef>
#include <map>
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
 *
 * Throws if `forcing_file` or any explicit mapping entry is empty.
 */
caf::config_value make_forcing_config(
    const std::string& forcing_file,
    const std::map<std::string, std::string>& variables_names_map = {});

/**
 * @brief One feature entry from a realization file.
 *
 * SYMFLUENCE realizations are expanded into one Fortran BMI task input per
 * catchment. Realization-driven C++ BMI modules are not supported yet.
 */
struct RealizationFeature {
    std::string library_file_;
    std::string init_config_path_;
    std::string forcing_file_;
    std::map<std::string, std::string> variables_names_map_;
};

/**
 * @brief Parsed realization used to build one task per feature.
 *
 * The supported realization format is SYMFLUENCE's global NGEN realization
 * with one `bmi_fortran` module and `CsvPerFeature` forcing.
 */
struct RealizationConfig {
    std::string output_root_;
    std::vector<RealizationFeature> features_;
};

/**
 * @brief Read a SYMFLUENCE NGEN realization file.
 *
 * Relative paths are resolved against the realization file's parent directory.
 * The parser expands sibling `catchments.geojson` features into realization
 * feature tasks.
 */
RealizationConfig read_realization_config(const std::string& realization_path);

} // namespace miaow
