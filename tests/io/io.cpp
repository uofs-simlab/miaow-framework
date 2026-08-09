#define BOOST_TEST_MODULE miaow_io
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "miaow_io.hpp"

namespace {

// Convert owned strings to the mutable argument array expected by the CLI parser
void parse_cli_arguments(std::vector<std::string> arguments,
                         miaow::RuntimeOptions& runtime_options) {
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argument_pointers.push_back(argument.data());
    }

    miaow::parse_runtime_cli_options(static_cast<int>(argument_pointers.size()),
                                     argument_pointers.data(), runtime_options);
}

} // namespace

// Accept the required working directory without producing an error
BOOST_AUTO_TEST_CASE(expected_run_directory_is_accepted) {
    BOOST_TEST(miaow::ensure_expected_run_dir(std::filesystem::current_path().string()));
}

// Report a working-directory mismatch with actionable guidance
BOOST_AUTO_TEST_CASE(unexpected_run_directory_is_reported) {
    std::ostringstream error_output;
    std::streambuf* original_error_buffer = std::cerr.rdbuf(error_output.rdbuf());
    const bool directory_matches = miaow::ensure_expected_run_dir(
        (std::filesystem::current_path() / "missing").string());
    std::cerr.rdbuf(original_error_buffer);

    BOOST_TEST(!directory_matches);
    BOOST_TEST(error_output.str().find("Run directory error: expected") !=
               std::string::npos);
}

// Apply every supported CLI option to its corresponding runtime setting
BOOST_AUTO_TEST_CASE(runtime_cli_options_are_parsed) {
    miaow::RuntimeOptions runtime_options;

    parse_cli_arguments({"miaow_example", "--max_workers", "3", "--task_request_timeout",
                         "7", "--job_request_timeout", "11", "--output_root", "results"},
                        runtime_options);

    BOOST_TEST(runtime_options.max_workers_ == 3);
    BOOST_CHECK_MESSAGE(runtime_options.task_request_timeout_ == std::chrono::seconds(7),
                        "task request timeout should be 7 seconds");
    BOOST_CHECK_MESSAGE(runtime_options.job_request_timeout_ == std::chrono::seconds(11),
                        "job request timeout should be 11 seconds");
    BOOST_TEST(runtime_options.output_root_ == "results");
}

// Reject a non-positive worker count with the public CLI error message
BOOST_AUTO_TEST_CASE(invalid_runtime_cli_value_is_rejected) {
    miaow::RuntimeOptions runtime_options;

    BOOST_CHECK_EXCEPTION(parse_cli_arguments({"miaow_example", "--max_workers", "0"},
                                              runtime_options),
                          std::runtime_error, [](const std::runtime_error& exception) {
                              return std::string(exception.what()) ==
                                     "Error: --max_workers requires a positive integer";
                          });
}
