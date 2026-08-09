#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <caf/json.hpp>

#include "miaow.hpp"

namespace miaow::io {

std::string trim(std::string value);

/**
 * @brief Write a CAF-serializable value as formatted JSON.
 *
 * `value` must be serializable by `caf::json_builder`: either a CAF-supported
 * type or a type with a valid `inspect(Inspector&, T&)` overload.
 */
template <class Value>
void write_json_file(const std::filesystem::path& path, const Value& value,
                     bool skip_empty_fields = true) {
    caf::json_builder builder;
    builder.skip_object_type_annotation(true);
    builder.skip_empty_fields(skip_empty_fields);
    if (!builder.apply(value)) {
        throw std::runtime_error("failed to serialize JSON file '" + path.string() +
                                 "': " + caf::to_string(builder.get_error()));
    }

    const caf::json_value json_value = builder.seal();
    std::string json_text;
    json_value.print_to(json_text, 2);

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" + path.string() + "'");
    }
    output << json_text << "\n";
}

// Throw validation errors with the caller's domain prefix
[[noreturn]] inline void throw_config_error(std::string_view error_prefix,
                                            const std::string& message) {
    throw std::runtime_error(std::string(error_prefix) + message);
}

std::vector<std::string> split_csv_line(const std::string& line);

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp);

} // namespace miaow::io
