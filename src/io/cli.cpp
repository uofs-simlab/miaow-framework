#include "miaow_io.hpp"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace miaow {

namespace {

// Parse one complete positive integer option value
std::size_t parse_positive_integer_option(std::string_view value,
                                          std::string_view option_name) {
    std::size_t parsed_value = 0;
    const std::from_chars_result parse_result =
        std::from_chars(value.data(), value.data() + value.size(), parsed_value);
    if (parse_result.ec != std::errc() || parse_result.ptr != value.data() + value.size() ||
        parsed_value == 0) {
        throw std::runtime_error("Error: " + std::string(option_name) +
                                 " requires a positive integer");
    }
    return parsed_value;
}

// Return the following option value and advance past the option-value pair
std::string_view read_option_value(int argc, char* argv[], int& index,
                                   std::string_view option_name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Error: " + std::string(option_name) +
                                 " requires a value");
    }

    const std::string_view value = argv[index + 1];
    if (value.starts_with("--")) {
        throw std::runtime_error("Error: " + std::string(option_name) +
                                 " requires a value");
    }
    index += 2;
    return value;
}

} // namespace

bool ensure_expected_run_dir(const std::string& expected_run_dir) {
    const std::filesystem::path expected_path(expected_run_dir);
    const std::filesystem::path actual_path = std::filesystem::current_path();

    std::error_code error_code;
    const bool run_dir_matches =
        std::filesystem::equivalent(actual_path, expected_path, error_code);
    if (!error_code && run_dir_matches) {
        return true;
    }

    std::cerr << "Run directory error: expected '" << expected_path.string() << "'\n"
              << "Current directory: '" << actual_path.string() << "'\n"
              << "Use:\n"
              << "  cd " << expected_path.string() << "\n";
    return false;
}

void parse_runtime_cli_options(int argc, char* argv[], RuntimeOptions& runtime_options) {
    int index = 1;
    while (index < argc) {
        const std::string_view option_name = argv[index];

        if (option_name == "--max_workers") {
            const std::string_view option_value =
                read_option_value(argc, argv, index, option_name);
            runtime_options.max_workers_ =
                parse_positive_integer_option(option_value, option_name);
            continue;
        }

        if (option_name == "--task_request_timeout") {
            const std::string_view option_value =
                read_option_value(argc, argv, index, option_name);
            runtime_options.task_request_timeout_ = std::chrono::seconds(
                parse_positive_integer_option(option_value, option_name));
            continue;
        }

        if (option_name == "--job_request_timeout") {
            const std::string_view option_value =
                read_option_value(argc, argv, index, option_name);
            runtime_options.job_request_timeout_ = std::chrono::seconds(
                parse_positive_integer_option(option_value, option_name));
            continue;
        }

        if (option_name == "--output_root") {
            runtime_options.output_root_ =
                read_option_value(argc, argv, index, option_name);
            continue;
        }

        throw std::runtime_error("Error: unrecognized argument: " +
                                 std::string(option_name));
    }
}

} // namespace miaow
