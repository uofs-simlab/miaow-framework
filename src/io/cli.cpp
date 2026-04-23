#include "miaow_io.hpp"

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace miaow {

namespace {

void parse_positive_integer_option(std::string_view value,
                                   std::string_view option_name,
                                   std::size_t& output) {
    const std::from_chars_result parse_result =
        std::from_chars(value.data(), value.data() + value.size(), output);
    if (parse_result.ec != std::errc() ||
        parse_result.ptr != value.data() + value.size() ||
        output == 0) {
        throw std::runtime_error(
            "Error: " + std::string(option_name) +
            " requires a positive integer");
    }
}

void require_option_value(int argc,
                          char* argv[],
                          int& index,
                          std::string_view option_name,
                          std::string& value) {
    if (index + 1 >= argc) {
        throw std::runtime_error(
            "Error: " + std::string(option_name) + " requires a value");
    }

    value = argv[index + 1];
    if (value.rfind("--", 0) == 0) {
        throw std::runtime_error(
            "Error: " + std::string(option_name) + " requires a value");
    }
    index += 2;
}

} // namespace

bool ensure_expected_run_dir(const std::string& executable_name,
                             const std::string& expected_run_dir) {
    const std::filesystem::path expected_path(expected_run_dir);
    const std::filesystem::path actual_path = std::filesystem::current_path();

    std::error_code error_code;
    const bool run_dir_matches =
        std::filesystem::equivalent(actual_path, expected_path, error_code);
    if (!error_code && run_dir_matches) {
        return true;
    }

    std::cerr
        << "Run directory error: " << executable_name << " must be run from '"
        << expected_path.string() << "'\n"
        << "Current directory: '" << actual_path.string() << "'\n"
        << "Use:\n"
        << "  cd " << expected_path.string() << "\n"
        << "  ./" << executable_name << "\n";
    return false;
}

void parse_runtime_cli_options(int argc,
                               char* argv[],
                               RuntimeOptions& runtime_options) {
    int index = 1;
    while (index < argc) {
        const std::string_view option_name = argv[index];
        std::string option_value;

        if (option_name == "--max_workers") {
            require_option_value(argc, argv, index, option_name, option_value);
            std::size_t parsed_workers = 0;
            parse_positive_integer_option(option_value, option_name, parsed_workers);
            runtime_options.max_workers_ = parsed_workers;
            continue;
        }

        if (option_name == "--task_request_timeout") {
            require_option_value(argc, argv, index, option_name, option_value);
            std::size_t parsed_seconds = 0;
            parse_positive_integer_option(option_value, option_name, parsed_seconds);
            runtime_options.task_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--job_request_timeout") {
            require_option_value(argc, argv, index, option_name, option_value);
            std::size_t parsed_seconds = 0;
            parse_positive_integer_option(option_value, option_name, parsed_seconds);
            runtime_options.job_request_timeout_ = std::chrono::seconds(parsed_seconds);
            continue;
        }

        if (option_name == "--output_root") {
            require_option_value(argc, argv, index, option_name, option_value);
            runtime_options.output_root_ = option_value;
            continue;
        }
        throw std::runtime_error(
            "Error: unrecognized argument: " + std::string(option_name));
    }
}

std::string parse_realization_cli_path(
    int argc,
    char* argv[],
    const std::string& executable_name,
    const std::string& default_realization_path) {
    std::string realization_path = default_realization_path;
    int index = 1;
    while (index < argc) {
        const std::string_view option_name = argv[index];
        if (option_name == "--realization") {
            std::string option_value;
            require_option_value(argc, argv, index, option_name, option_value);
            realization_path = option_value;
            continue;
        }
        if (option_name == "--help") {
            std::cout << "Usage: " << executable_name
                      << " [--realization <path>]\n";
            std::exit(0);
        }
        throw std::runtime_error("Error: unrecognized argument: " +
                                 std::string(option_name));
    }
    return realization_path;
}

} // namespace miaow
