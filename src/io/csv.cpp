#include "miaow_io.hpp"

#include "io/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) {
    static constexpr int days_per_month[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days_per_month[month - 1];
}

int parse_int_component(const std::string& raw_timestamp,
                        std::size_t start,
                        std::size_t length) {
    const std::string component = raw_timestamp.substr(start, length);
    if (!std::all_of(component.begin(), component.end(),
                     [](unsigned char ch) { return std::isdigit(ch); })) {
        throw std::runtime_error("invalid");
    }
    return std::stoi(component);
}

std::int64_t days_since_unix_epoch(int year, unsigned month, unsigned day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        (153 * (month + (month > 2 ? -3U : 9U)) + 2) / 5 + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 +
           static_cast<std::int64_t>(day_of_era) - 719468;
}

std::tuple<int, unsigned, unsigned> civil_from_days(
    std::int64_t days_since_epoch) {
    days_since_epoch += 719468;
    const std::int64_t era =
        (days_since_epoch >= 0 ? days_since_epoch : days_since_epoch - 146096) /
        146097;
    const unsigned day_of_era =
        static_cast<unsigned>(days_since_epoch - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
         day_of_era / 146096) /
        365;
    int year = static_cast<int>(year_of_era) + static_cast<int>(era) * 400;
    const unsigned day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 -
                      year_of_era / 100);
    const unsigned month_prime = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    const unsigned month = month_prime + (month_prime < 10 ? 3U : -9U);
    year += month <= 2 ? 1 : 0;
    return {year, month, day};
}

} // namespace

namespace miaow::io {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    if (begin == value.end()) {
        return "";
    }
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(begin, end);
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cells;
    std::stringstream line_stream(line);
    std::string cell;
    while (std::getline(line_stream, cell, ',')) {
        cells.push_back(trim(cell));
    }
    return cells;
}

std::vector<std::string> read_csv_header(const std::string& csv_path) {
    std::ifstream csv_stream(csv_path);
    if (!csv_stream.is_open()) {
        throw std::runtime_error("unable to open CSV file '" + csv_path + "'");
    }

    std::string header_line;
    if (!std::getline(csv_stream, header_line)) {
        throw std::runtime_error("CSV file '" + csv_path + "' is missing a header row");
    }

    return split_csv_line(header_line);
}

std::size_t find_csv_column_index(const std::vector<std::string>& headers,
                                  const std::string& column_name) {
    const auto it = std::find(headers.begin(), headers.end(), column_name);
    if (it == headers.end()) {
        throw std::runtime_error("column '" + column_name + "' not found in CSV header");
    }
    return static_cast<std::size_t>(std::distance(headers.begin(), it));
}

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp) {
    const std::string timestamp = trim(raw_timestamp);
    if (timestamp.size() != 19 ||
        (timestamp[4] != '-' && timestamp[4] != '/') ||
        timestamp[7] != timestamp[4] ||
        timestamp[10] != ' ' ||
        timestamp[13] != ':' ||
        timestamp[16] != ':') {
        throw std::runtime_error("invalid");
    }

    const int year = parse_int_component(timestamp, 0, 4);
    const int month = parse_int_component(timestamp, 5, 2);
    const int day = parse_int_component(timestamp, 8, 2);
    const int hour = parse_int_component(timestamp, 11, 2);
    const int minute = parse_int_component(timestamp, 14, 2);
    const int second = parse_int_component(timestamp, 17, 2);

    if (month < 1 || month > 12) {
        throw std::runtime_error("invalid");
    }
    if (day < 1 || day > days_in_month(year, month)) {
        throw std::runtime_error("invalid");
    }
    if (hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        throw std::runtime_error("invalid");
    }

    return days_since_unix_epoch(year, static_cast<unsigned>(month),
                                 static_cast<unsigned>(day)) * 86400 +
           static_cast<std::int64_t>(hour) * 3600 +
           static_cast<std::int64_t>(minute) * 60 + second;
}

std::string format_timestamp_seconds(std::int64_t seconds_since_epoch,
                                     char date_separator) {
    if (date_separator != '-' && date_separator != '/') {
        throw std::runtime_error("invalid");
    }

    std::int64_t days = seconds_since_epoch / 86400;
    std::int64_t seconds_of_day = seconds_since_epoch % 86400;
    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        --days;
    }

    const auto [year, month, day] = civil_from_days(days);
    const int hour = static_cast<int>(seconds_of_day / 3600);
    const int minute = static_cast<int>((seconds_of_day % 3600) / 60);
    const int second = static_cast<int>(seconds_of_day % 60);

    std::ostringstream timestamp;
    timestamp << std::setfill('0')
              << std::setw(4) << year << date_separator
              << std::setw(2) << month << date_separator
              << std::setw(2) << day << " "
              << std::setw(2) << hour << ":"
              << std::setw(2) << minute << ":"
              << std::setw(2) << second;
    return timestamp.str();
}

std::vector<double> read_csv_numeric_column(const std::string& csv_path,
                                            const std::string& column_name) {
    std::ifstream csv_stream(csv_path);
    if (!csv_stream.is_open()) {
        throw std::runtime_error("unable to open CSV file '" + csv_path + "'");
    }

    std::string header_line;
    if (!std::getline(csv_stream, header_line)) {
        throw std::runtime_error("CSV file '" + csv_path + "' is missing a header row");
    }

    const std::vector<std::string> headers = split_csv_line(header_line);
    const std::size_t column_index = find_csv_column_index(headers, column_name);

    std::vector<double> values;
    std::string row_line;
    std::size_t line_number = 1;
    while (std::getline(csv_stream, row_line)) {
        ++line_number;
        if (trim(row_line).empty()) {
            continue;
        }

        const std::vector<std::string> cells = split_csv_line(row_line);
        if (column_index >= cells.size()) {
            throw std::runtime_error(
                "row " + std::to_string(line_number) +
                " is missing column '" + column_name + "'");
        }

        const std::string& raw_value = cells.at(column_index);
        std::size_t parse_pos = 0;
        double value = 0.0;
        try {
            value = std::stod(raw_value, &parse_pos);
        } catch (...) {
            throw std::runtime_error(
                "non-numeric value in column '" + column_name +
                "' at row " + std::to_string(line_number));
        }

        if (parse_pos != raw_value.size()) {
            throw std::runtime_error(
                "non-numeric value in column '" + column_name +
                "' at row " + std::to_string(line_number));
        }
        if (!std::isfinite(value)) {
            throw std::runtime_error(
                "non-finite value in column '" + column_name +
                "' at row " + std::to_string(line_number));
        }

        values.push_back(value);
    }

    if (values.empty()) {
        throw std::runtime_error("CSV file '" + csv_path + "' contains no data rows");
    }

    return values;
}

} // namespace miaow::io

namespace miaow {

caf::config_value make_forcing_config(
    const std::string& forcing_file,
    const std::map<std::string, std::string>& variables_names_map) {
    if (io::trim(forcing_file).empty()) {
        throw std::runtime_error(
            "Forcing config error: forcing_file must be a non-empty string");
    }

    caf::config_value::dictionary forcing_config;
    forcing_config.insert("forcing_file", forcing_file);

    if (!variables_names_map.empty()) {
        caf::config_value::dictionary forcing_names_map;
        for (const auto& [input_name, csv_header] : variables_names_map) {
            if (io::trim(input_name).empty()) {
                throw std::runtime_error(
                    "Forcing config error: variables_names_map contains an "
                    "empty BMI input variable name");
            }
            if (io::trim(csv_header).empty()) {
                throw std::runtime_error(
                    "Forcing config error: variables_names_map entry '" +
                    input_name + "' must be a non-empty string");
            }
            forcing_names_map.insert(input_name, csv_header);
        }
        forcing_config.insert("variables_names_map", std::move(forcing_names_map));
    }

    return caf::config_value{std::move(forcing_config)};
}

std::string write_repeated_hourly_forcing_csv(const std::string& source_path,
                                              const std::string& output_filename,
                                              std::size_t total_rows) {
    std::ifstream source_stream(source_path);
    if (!source_stream.is_open()) {
        throw std::runtime_error(
            "unable to open source forcing CSV '" + source_path + "'");
    }

    std::string header;
    if (!std::getline(source_stream, header)) {
        throw std::runtime_error(
            "source forcing CSV '" + source_path + "' is missing a header row");
    }

    std::vector<std::string> row_suffixes;
    row_suffixes.reserve(24);
    std::string first_timestamp;
    std::string line;
    while (std::getline(source_stream, line)) {
        if (io::trim(line).empty()) {
            continue;
        }

        const std::size_t comma_pos = line.find(',');
        if (comma_pos == std::string::npos) {
            throw std::runtime_error(
                "source forcing CSV '" + source_path + "' is missing data columns");
        }

        if (first_timestamp.empty()) {
            first_timestamp = io::trim(line.substr(0, comma_pos));
        }
        row_suffixes.push_back(line.substr(comma_pos + 1));
    }

    if (row_suffixes.empty()) {
        throw std::runtime_error(
            "source forcing CSV '" + source_path + "' contains no data rows");
    }

    std::int64_t base_seconds = 0;
    try {
        base_seconds = io::parse_timestamp_seconds(first_timestamp);
    } catch (const std::exception&) {
        throw std::runtime_error(
            "source forcing CSV '" + source_path +
            "' has an invalid timestamp in its first data row");
    }

    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() / output_filename;
    std::ofstream output_stream(output_path);
    if (!output_stream.is_open()) {
        throw std::runtime_error(
            "unable to create generated forcing CSV '" + output_path.string() +
            "'");
    }

    const char date_separator = first_timestamp.at(4);
    output_stream << header << "\n";
    for (std::size_t row = 0; row < total_rows; ++row) {
        output_stream
            << io::format_timestamp_seconds(
                   base_seconds + static_cast<std::int64_t>(row) * 3600,
                   date_separator)
            << "," << row_suffixes.at(row % row_suffixes.size()) << "\n";
    }

    return output_path.string();
}

} // namespace miaow
