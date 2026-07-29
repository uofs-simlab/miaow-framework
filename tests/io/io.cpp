#define BOOST_TEST_MODULE miaow_io
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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
    BOOST_TEST(runtime_options.task_request_timeout_ == std::chrono::seconds(7));
    BOOST_TEST(runtime_options.job_request_timeout_ == std::chrono::seconds(11));
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

// Repeat source values cyclically while advancing generated timestamps by one hour
BOOST_AUTO_TEST_CASE(repeated_hourly_forcing_csv_preserves_cycle) {
    const std::filesystem::path source_path =
        std::filesystem::temp_directory_path() / "miaow_hourly_forcing_source.csv";
    constexpr const char* output_filename = "miaow_hourly_forcing_output.csv";
    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() / output_filename;
    std::filesystem::remove(source_path);
    std::filesystem::remove(output_path);

    std::ofstream source_stream(source_path);
    BOOST_REQUIRE(source_stream.is_open());
    source_stream << "time,value\n"
                  << "2020-01-01 00:00:00,10\n"
                  << "2020-01-01 01:00:00,20\n";
    source_stream.close();

    const std::string generated_path =
        miaow::write_repeated_hourly_forcing_csv(source_path.string(), output_filename, 5);
    std::ifstream generated_stream(generated_path);
    BOOST_REQUIRE(generated_stream.is_open());
    std::ostringstream generated_contents;
    generated_contents << generated_stream.rdbuf();
    generated_stream.close();

    BOOST_TEST(generated_contents.str() == "time,value\n"
                                           "2020-01-01 00:00:00,10\n"
                                           "2020-01-01 01:00:00,20\n"
                                           "2020-01-01 02:00:00,10\n"
                                           "2020-01-01 03:00:00,20\n"
                                           "2020-01-01 04:00:00,10\n");

    std::filesystem::remove(source_path);
    std::filesystem::remove(output_path);
}

// Report an invalid first timestamp before creating generated forcing output
BOOST_AUTO_TEST_CASE(malformed_forcing_csv_is_rejected) {
    const std::filesystem::path source_path =
        std::filesystem::temp_directory_path() / "miaow_malformed_forcing_source.csv";
    constexpr const char* output_filename = "miaow_malformed_forcing_output.csv";
    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() / output_filename;
    std::filesystem::remove(source_path);
    std::filesystem::remove(output_path);

    std::ofstream source_stream(source_path);
    BOOST_REQUIRE(source_stream.is_open());
    source_stream << "time,value\n"
                  << "not-a-timestamp,10\n";
    source_stream.close();

    BOOST_CHECK_EXCEPTION(
        miaow::write_repeated_hourly_forcing_csv(source_path.string(), output_filename, 2),
        std::runtime_error, [](const std::runtime_error& exception) {
            return std::string(exception.what())
                       .find("has an invalid timestamp in its first data row") !=
                   std::string::npos;
        });
    BOOST_TEST(!std::filesystem::exists(output_path));

    std::filesystem::remove(source_path);
    std::filesystem::remove(output_path);
}
