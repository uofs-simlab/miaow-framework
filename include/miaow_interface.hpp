#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "miaow.hpp"

namespace miaow {

/**
 * @brief Return usage text for workflow examples
 */
inline std::string workflow_cli_usage(const std::string& program_name) {
    return "Usage: " + program_name +
           " --config <config_file_1> [<config_file_2> ...]"
           " [--max_workers <n>] [--task_request_timeout <seconds>]"
           " [--job_request_timeout <seconds>]";
}

/**
 * @brief Parse common workflow CLI arguments
 *
 * Supported arguments:
 * - `--config <config_file_1> [<config_file_2> ...]`
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 */
inline std::string parse_workflow_cli_options(int argc,
                                              char* argv[],
                                              RuntimeOptions& runtime_options,
                                              std::vector<std::string>& config_files) {
    auto parse_positive_integer =
        [](const std::string& value,
           const std::string& option_name,
           std::size_t& output) -> std::string {
            std::size_t parsed_value = 0;
            const auto parse_result =
                std::from_chars(value.data(), value.data() + value.size(), parsed_value);
            if (parse_result.ec != std::errc() ||
                parse_result.ptr != value.data() + value.size() ||
                parsed_value == 0) {
                return "Error: " + option_name + " requires a positive integer";
            }
            output = parsed_value;
            return "";
        };

    auto ensure_single_value =
        [](const std::string& option_name,
           const std::vector<std::string>& values) -> std::string {
            if (values.size() != 1) {
                return "Error: " + option_name + " requires exactly one value";
            }
            return "";
        };

    std::unordered_map<std::string, std::function<std::string(const std::vector<std::string>&)>>
        option_handlers;
    std::vector<std::string> seen_options;

    option_handlers["--config"] = [&](const std::vector<std::string>& values) -> std::string {
        if (std::find(seen_options.begin(), seen_options.end(), "--config") !=
            seen_options.end()) {
            return "Error: --config may only be provided once";
        }
        if (values.empty()) {
            return "Error: --config requires at least one config file";
        }
        config_files = values;
        seen_options.push_back("--config");
        return "";
    };

    option_handlers["--max_workers"] = [&](const std::vector<std::string>& values) -> std::string {
        if (std::find(seen_options.begin(), seen_options.end(), "--max_workers") !=
            seen_options.end()) {
            return "Error: --max_workers may only be provided once";
        }
        const std::string value_count_error = ensure_single_value("--max_workers", values);
        if (!value_count_error.empty()) {
            return value_count_error;
        }
        std::size_t parsed_workers = 0;
        const std::string parse_error =
            parse_positive_integer(values.front(), "--max_workers", parsed_workers);
        if (!parse_error.empty()) {
            return parse_error;
        }
        runtime_options.max_workers_ = parsed_workers;
        seen_options.push_back("--max_workers");
        return "";
    };

    option_handlers["--task_request_timeout"] =
        [&](const std::vector<std::string>& values) -> std::string {
            if (std::find(seen_options.begin(), seen_options.end(), "--task_request_timeout") !=
                seen_options.end()) {
                return "Error: --task_request_timeout may only be provided once";
            }
            const std::string value_count_error =
                ensure_single_value("--task_request_timeout", values);
            if (!value_count_error.empty()) {
                return value_count_error;
            }
            std::size_t parsed_seconds = 0;
            const std::string parse_error = parse_positive_integer(
                values.front(), "--task_request_timeout", parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.task_request_timeout_ = std::chrono::seconds(parsed_seconds);
            seen_options.push_back("--task_request_timeout");
            return "";
        };

    option_handlers["--job_request_timeout"] =
        [&](const std::vector<std::string>& values) -> std::string {
            if (std::find(seen_options.begin(), seen_options.end(), "--job_request_timeout") !=
                seen_options.end()) {
                return "Error: --job_request_timeout may only be provided once";
            }
            const std::string value_count_error =
                ensure_single_value("--job_request_timeout", values);
            if (!value_count_error.empty()) {
                return value_count_error;
            }
            std::size_t parsed_seconds = 0;
            const std::string parse_error = parse_positive_integer(
                values.front(), "--job_request_timeout", parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.job_request_timeout_ = std::chrono::seconds(parsed_seconds);
            seen_options.push_back("--job_request_timeout");
            return "";
        };

    int index = 1;
    while (index < argc) {
        const std::string option_name = argv[index];
        if (option_name.rfind("--", 0) != 0) {
            return "Error: unrecognized positional argument: " + option_name;
        }

        const auto handler_it = option_handlers.find(option_name);
        if (handler_it == option_handlers.end()) {
            return "Error: unrecognized argument: " + option_name;
        }

        std::vector<std::string> option_values;
        ++index;
        if (option_name == "--config") {
            while (index < argc) {
                const std::string value = argv[index];
                if (value.rfind("--", 0) == 0) {
                    break;
                }
                option_values.push_back(value);
                ++index;
            }
        } else {
            if (index >= argc) {
                return "Error: " + option_name + " requires a value";
            }
            const std::string value = argv[index];
            if (value.rfind("--", 0) == 0) {
                return "Error: " + option_name + " requires a value";
            }
            option_values.push_back(value);
            ++index;
        }

        const std::string handler_error = handler_it->second(option_values);
        if (!handler_error.empty()) {
            return handler_error;
        }
    }

    if (config_files.empty()) {
        return "Error: missing required --config argument";
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
    std::vector<std::string>& config_files) {
    const std::string parse_error =
        parse_workflow_cli_options(argc, argv, runtime_options, config_files);
    if (!parse_error.empty()) {
        return parse_error;
    }
    return validate_config_paths(config_files);
}

} // namespace miaow
