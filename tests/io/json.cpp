#include <nlohmann/json.hpp>
#include <boost/test/unit_test.hpp>

#include <caf/init_global_meta_objects.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "io/internal.hpp"

// Saved numerical output must preserve small values and the full finite double range.
BOOST_AUTO_TEST_CASE(json_files_preserve_nested_output_and_double_precision) {
    caf::core::init_global_meta_objects();
    const std::vector<double> values{
        1e-8, -1e-8, 1.2345678901234567, 1.2345678901234567e-12,
        1e-100, 1e100, 1.0, 0.0, -0.0,
        std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    caf::config_value::dictionary output;
    output.emplace("values", values);
    output.emplace("label", "quoted \"text\"\nsecond line");
    output.emplace("enabled", true);
    output.emplace("integer", std::numeric_limits<std::int64_t>::min());
    output.emplace("empty_list", caf::config_value::list{});
    output.emplace("empty_object", caf::config_value::dictionary{});
    output.emplace("non_finite", std::vector<double>{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()});
    const miaow::TaskResult result{miaow::TaskStatus::succeeded, "numerical output",
                                   caf::config_value{std::move(output)}};

    const auto directory = std::filesystem::path{MIAOW_IO_TEST_OUTPUT_ROOT};
    std::filesystem::create_directories(directory);
    const auto path = directory / "double_precision.json";
    miaow::io::write_json_file(path, result, false);

    std::ifstream input{path};
    const auto json = nlohmann::json::parse(input);
    const auto& stored = json.at("output");
    const auto& stored_values = stored.at("values").at("value");
    BOOST_REQUIRE_EQUAL(stored_values.size(), values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        BOOST_TEST_CONTEXT("value index = " << index) {
            const double saved = stored_values[index].at("value").get<double>();
            BOOST_TEST(saved == values[index]);
            BOOST_TEST(std::signbit(saved) == std::signbit(values[index]));
        }
    }
    BOOST_TEST(json.at("status").get<std::string>() == "succeeded");
    BOOST_TEST(stored.at("label").at("value").get<std::string>()
               == "quoted \"text\"\nsecond line");
    BOOST_TEST(stored.at("enabled").at("value").get<bool>());
    BOOST_TEST(stored.at("integer").at("value").get<std::int64_t>()
               == std::numeric_limits<std::int64_t>::min());
    BOOST_TEST(stored.at("empty_list").at("value").is_array());
    BOOST_TEST(stored.at("empty_object").at("value").is_object());
    BOOST_TEST(stored.at("empty_list").at("value").empty());
    BOOST_TEST(stored.at("empty_object").at("value").empty());
    // Non-finite doubles retain the existing JSON null representation.
    for (const auto& item : stored.at("non_finite").at("value")) {
        BOOST_TEST(item.at("value").is_null());
    }
    std::filesystem::remove(path);
}

// Job records retain unsigned IDs, duration strings, and the configured empty fields.
BOOST_AUTO_TEST_CASE(json_files_preserve_record_schema) {
    const miaow::JobRecord record{std::numeric_limits<std::uint64_t>::max(), 7, {},
                                   std::chrono::seconds{2}};
    const auto directory = std::filesystem::path{MIAOW_IO_TEST_OUTPUT_ROOT};
    std::filesystem::create_directories(directory);
    const auto path = directory / "record_schema.json";
    miaow::io::write_json_file(path, record, false);

    std::ifstream input{path};
    const auto json = nlohmann::json::parse(input);
    BOOST_TEST(json.at("run_id").get<std::uint64_t>() == record.run_id_);
    BOOST_TEST(json.at("job_id").get<miaow::JobId>() == record.job_id_);
    BOOST_TEST(json.at("run_time").get<std::string>() == "2s");
    BOOST_TEST(json.at("tasks").is_array());
    BOOST_TEST(json.at("tasks").empty());
    BOOST_TEST(!json.contains("@type"));
    std::filesystem::remove(path);
}
