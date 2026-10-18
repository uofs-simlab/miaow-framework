#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

#include <caf/init_global_meta_objects.hpp>
#include <caf/json_reader.hpp>

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
    const miaow::TaskResult result{miaow::TaskStatus::succeeded, "numerical output",
                                   caf::config_value{std::move(output)}};

    const auto directory = std::filesystem::path{MIAOW_IO_TEST_OUTPUT_ROOT};
    std::filesystem::create_directories(directory);
    const auto path = directory / "double_precision.json";
    miaow::io::write_json_file(path, result, false);

    // Precise parsing checks the writer independently of CAF's decimal conversion.
    boost::json::parse_options options;
    options.numbers = boost::json::number_precision::precise;
    std::ifstream input{path};
    const auto json = boost::json::parse(input, {}, options);
    const auto& stored = json.at("output");
    const auto& stored_values = stored.at("values").at("value").as_array();
    BOOST_REQUIRE_EQUAL(stored_values.size(), values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        BOOST_TEST_CONTEXT("value index = " << index) {
            const double saved = stored_values[index].at("value").to_number<double>();
            BOOST_TEST(saved == values[index]);
            BOOST_TEST(std::signbit(saved) == std::signbit(values[index]));
        }
    }
    BOOST_TEST(json.at("status").as_string() == "succeeded");
    BOOST_TEST(stored.at("label").at("value").as_string()
               == "quoted \"text\"\nsecond line");
    BOOST_TEST(stored.at("enabled").at("value").as_bool());
    BOOST_TEST(stored.at("integer").at("value").as_int64()
               == std::numeric_limits<std::int64_t>::min());
    BOOST_TEST(stored.at("empty_list").at("value").as_array().empty());
    BOOST_TEST(stored.at("empty_object").at("value").as_object().empty());
    std::filesystem::remove(path);
}

// CAF readback must accept the saved schema and preserve both signs of zero.
BOOST_AUTO_TEST_CASE(json_files_preserve_caf_readback_compatibility) {
    caf::core::init_global_meta_objects();
    const auto directory = std::filesystem::path{MIAOW_IO_TEST_OUTPUT_ROOT};
    std::filesystem::create_directories(directory);
    const auto path = directory / "caf_readback.json";
    for (const double value : {0.0, -0.0, 1.0, 1.2345678901234567, 1e-8, 1e-100}) {
        const miaow::TaskResult result{miaow::TaskStatus::succeeded, "value",
                                       caf::config_value{value}};
        miaow::io::write_json_file(path, result, false);
        caf::json_reader reader;
        BOOST_REQUIRE_MESSAGE(reader.load_file(path.string()),
                              caf::to_string(reader.get_error()));
        miaow::TaskResult restored;
        BOOST_REQUIRE_MESSAGE(reader.apply(restored), caf::to_string(reader.get_error()));
        const auto* saved = caf::get_if<caf::config_value::real>(&restored.output_);
        BOOST_REQUIRE(saved != nullptr);
        // CAF's reader may round while scaling a decimal mantissa.
        BOOST_TEST(*saved == value,
                   boost::test_tools::tolerance(
                       4 * std::numeric_limits<double>::epsilon()));
        BOOST_TEST(std::signbit(*saved) == std::signbit(value));
    }
    std::filesystem::remove(path);
}
