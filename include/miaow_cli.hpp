#pragma once

#include <charconv>
#include <cstddef>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "miaow.hpp"

namespace miaow {

/**
 * @brief Return usage text for workflow examples
 */
inline std::string workflow_cli_usage(const std::string& program_name) {
    return "Usage: " + program_name +
           " --config <config_file_1> [<config_file_2> ...]"
           " [--forcing_config <forcing_config_file_1> [<forcing_config_file_2> ...]]"
           " [--help]"
           " [--max_workers <n>] [--task_request_timeout <seconds>]"
           " [--job_request_timeout <seconds>] [--output_root <path>]";
}

/**
 * @brief Parse common workflow CLI arguments
 *
 * Supported arguments:
 * - `--config <config_file_1> [<config_file_2> ...]`
 * - `--forcing_config <forcing_config_file_1> [<forcing_config_file_2> ...]`
 * - `--help`
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 * - `--output_root <path>`
 */
inline std::string parse_workflow_cli_options(int argc,
                                              char* argv[],
                                              RuntimeOptions& runtime_options,
                                              std::vector<std::string>& config_files,
                                              std::vector<std::string>& forcing_config_files,
                                              bool& help_requested) {
    help_requested = false;
    config_files.clear();
    forcing_config_files.clear();

    const auto parse_positive_integer =
        [](const std::string& value,
           const std::string& option_name,
           std::size_t& output) -> std::string {
            std::size_t parsed_value = 0;
            const std::from_chars_result parse_result =
                std::from_chars(value.data(), value.data() + value.size(), parsed_value);
            if (parse_result.ec != std::errc() ||
                parse_result.ptr != value.data() + value.size() ||
                parsed_value == 0) {
                return "Error: " + option_name + " requires a positive integer";
            }
            output = parsed_value;
            return "";
        };

    const auto parse_single_value =
        [&](int& index, const std::string& option_name, std::string& value) -> std::string {
            if (index >= argc) {
                return "Error: " + option_name + " requires a value";
            }
            value = argv[index];
            if (value.rfind("--", 0) == 0) {
                return "Error: " + option_name + " requires a value";
            }
            ++index;
            return "";
        };

    bool seen_config = false;
    bool seen_forcing_config = false;
    bool seen_max_workers = false;
    bool seen_task_request_timeout = false;
    bool seen_job_request_timeout = false;
    bool seen_output_root = false;

    int index = 1;
    while (index < argc) {
        const std::string option_name = argv[index++];
        if (option_name == "--help" || option_name == "-h") {
            help_requested = true;
            return "";
        }

        if (option_name == "--config") {
            if (seen_config) {
                return "Error: --config may only be provided once";
            }
            seen_config = true;

            while (index < argc) {
                const std::string value = argv[index];
                if (value.rfind("--", 0) == 0) {
                    break;
                }
                config_files.push_back(value);
                ++index;
            }

            if (config_files.empty()) {
                return "Error: --config requires at least one config file";
            }
            continue;
        }

        if (option_name == "--forcing_config") {
            if (seen_forcing_config) {
                return "Error: --forcing_config may only be provided once";
            }
            seen_forcing_config = true;

            while (index < argc) {
                const std::string value = argv[index];
                if (value.rfind("--", 0) == 0) {
                    break;
                }
                forcing_config_files.push_back(value);
                ++index;
            }

            if (forcing_config_files.empty()) {
                return "Error: --forcing_config requires at least one forcing config file";
            }
            continue;
        }

        std::string option_value;
        if (option_name == "--max_workers") {
            if (seen_max_workers) {
                return "Error: --max_workers may only be provided once";
            }
            seen_max_workers = true;

            const std::string value_error =
                parse_single_value(index, "--max_workers", option_value);
            if (!value_error.empty()) {
                return value_error;
            }

            std::size_t parsed_workers = 0;
            const std::string parse_error =
                parse_positive_integer(option_value, "--max_workers", parsed_workers);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.max_workers_ = parsed_workers;
            continue;
        }

        if (option_name == "--task_request_timeout") {
            if (seen_task_request_timeout) {
                return "Error: --task_request_timeout may only be provided once";
            }
            seen_task_request_timeout = true;

            const std::string value_error =
                parse_single_value(index, "--task_request_timeout", option_value);
            if (!value_error.empty()) {
                return value_error;
            }

            std::size_t parsed_seconds = 0;
            const std::string parse_error = parse_positive_integer(
                option_value, "--task_request_timeout", parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.task_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--job_request_timeout") {
            if (seen_job_request_timeout) {
                return "Error: --job_request_timeout may only be provided once";
            }
            seen_job_request_timeout = true;

            const std::string value_error =
                parse_single_value(index, "--job_request_timeout", option_value);
            if (!value_error.empty()) {
                return value_error;
            }

            std::size_t parsed_seconds = 0;
            const std::string parse_error = parse_positive_integer(
                option_value, "--job_request_timeout", parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.job_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--output_root") {
            if (seen_output_root) {
                return "Error: --output_root may only be provided once";
            }
            seen_output_root = true;

            const std::string value_error =
                parse_single_value(index, "--output_root", option_value);
            if (!value_error.empty()) {
                return value_error;
            }

            runtime_options.output_root_ = option_value;
            continue;
        }

        if (option_name.rfind("--", 0) == 0) {
            return "Error: unrecognized argument: " + option_name;
        }
        return "Error: unrecognized positional argument: " + option_name;
    }

    if (config_files.empty()) {
        return "Error: missing required --config argument";
    }

    if (!forcing_config_files.empty() &&
        forcing_config_files.size() != config_files.size()) {
        return "Error: --forcing_config count must match --config count";
    }

    return "";
}

/**
 * @brief Parse workflow CLI options and validate all config paths
 */
inline std::string parse_and_validate_workflow_cli_options(
    int argc,
    char* argv[],
    RuntimeOptions& runtime_options,
    std::vector<std::string>& config_files,
    std::vector<std::string>& forcing_config_files,
    bool& help_requested) {
    const std::string parse_error =
        parse_workflow_cli_options(
            argc, argv, runtime_options, config_files, forcing_config_files,
            help_requested);
    if (!parse_error.empty()) {
        return parse_error;
    }

    if (help_requested) {
        return "";
    }

    const std::string config_path_error = validate_config_paths(config_files);
    if (!config_path_error.empty()) {
        return config_path_error;
    }

    for (const std::string& forcing_config_file : forcing_config_files) {
        std::ifstream input(forcing_config_file);
        if (!input.good()) {
            return std::string("Error: could not open forcing config file: ") +
                   forcing_config_file;
        }
    }

    return "";
}

} // namespace miaow
