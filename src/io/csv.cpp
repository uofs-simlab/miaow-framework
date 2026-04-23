#include "io/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
