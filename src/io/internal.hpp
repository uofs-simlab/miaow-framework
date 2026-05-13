#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <caf/json.hpp>

#include "miaow.hpp"

namespace miaow::io {

/**
 * @brief Write a CAF-inspectable value as formatted JSON.
 *
 * `value` must be serializable by `caf::json_builder`: either a CAF-supported
 * type or a type with a valid `inspect(Inspector&, T&)` overload.
 */
template <class Value>
void write_json_file(const std::filesystem::path& path,
                     const Value& value,
                     bool skip_empty_fields = true) {
    caf::json_builder builder;
    builder.skip_object_type_annotation(true);
    builder.skip_empty_fields(skip_empty_fields);
    if (!builder.apply(value)) {
        throw std::runtime_error("failed to serialize JSON file '" +
                                 path.string() + "': " +
                                 caf::to_string(builder.get_error()));
    }

    const caf::json_value json_value = builder.seal();
    std::string json_text;
    json_value.print_to(json_text, 2);

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" +
                                 path.string() + "'");
    }
    output << json_text << "\n";
}

std::string trim(std::string value);

std::vector<std::string> split_csv_line(const std::string& line);

std::vector<std::string> read_csv_header(const std::string& csv_path);

std::size_t find_csv_column_index(const std::vector<std::string>& headers,
                                  const std::string& column_name);

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp);

std::string format_timestamp_seconds(std::int64_t seconds_since_epoch,
                                     char date_separator);

std::vector<double> read_csv_numeric_column(const std::string& csv_path,
                                            const std::string& column_name);

std::string format_diagnostics(const JobRecord& job_record);

void write_job_record(const RuntimeOptions& runtime_options,
                      const JobRecord& job_record);

} // namespace miaow::io
