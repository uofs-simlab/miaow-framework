#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

#include "miaow.hpp"

namespace miaow {

static inline std::string parse_positive_integer_option(std::string_view value,
                                                        std::string_view option_name,
                                                        std::size_t& output) {
    const std::from_chars_result parse_result =
        std::from_chars(value.data(), value.data() + value.size(), output);
    if (parse_result.ec != std::errc() ||
        parse_result.ptr != value.data() + value.size() ||
        output == 0) {
        return "Error: " + std::string(option_name) + " requires a positive integer";
    }
    return "";
}

static inline std::string require_option_value(int argc,
                                               char* argv[],
                                               int& index,
                                               std::string_view option_name,
                                               std::string& value) {
    if (index + 1 >= argc) {
        return "Error: " + std::string(option_name) + " requires a value";
    }

    value = argv[index + 1];
    if (value.rfind("--", 0) == 0) {
        return "Error: " + std::string(option_name) + " requires a value";
    }
    index += 2;
    return "";
}

/**
 * @brief Parse runtime-wide CLI arguments without workflow task inputs
 *
 * Supported arguments:
 * - `--max_workers <num_workers>`
 * - `--task_request_timeout <seconds>`
 * - `--job_request_timeout <seconds>`
 * - `--output_root <path>`
 */
inline std::string parse_runtime_cli_options(int argc,
                                             char* argv[],
                                             RuntimeOptions& runtime_options) {
    int index = 1;
    while (index < argc) {
        const std::string_view option_name = argv[index];
        std::string option_value;

        if (option_name == "--max_workers") {
            const std::string value_error =
                require_option_value(argc, argv, index, option_name, option_value);
            if (!value_error.empty()) {
                return value_error;
            }
            std::size_t parsed_workers = 0;
            const std::string parse_error =
                parse_positive_integer_option(option_value, option_name, parsed_workers);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.max_workers_ = parsed_workers;
            continue;
        }

        if (option_name == "--task_request_timeout") {
            const std::string value_error =
                require_option_value(argc, argv, index, option_name, option_value);
            if (!value_error.empty()) {
                return value_error;
            }
            std::size_t parsed_seconds = 0;
            const std::string parse_error =
                parse_positive_integer_option(option_value, option_name, parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.task_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--job_request_timeout") {
            const std::string value_error =
                require_option_value(argc, argv, index, option_name, option_value);
            if (!value_error.empty()) {
                return value_error;
            }
            std::size_t parsed_seconds = 0;
            const std::string parse_error =
                parse_positive_integer_option(option_value, option_name, parsed_seconds);
            if (!parse_error.empty()) {
                return parse_error;
            }
            runtime_options.job_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--output_root") {
            const std::string value_error =
                require_option_value(argc, argv, index, option_name, option_value);
            if (!value_error.empty()) {
                return value_error;
            }
            runtime_options.output_root_ = option_value;
            continue;
        }
        return "Error: unrecognized argument: " + std::string(option_name);
    }
    return "";
}

} // namespace miaow
