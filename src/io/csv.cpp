#include "io/internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int parse_int_component(const std::string& raw_timestamp, std::size_t start,
                        std::size_t length) {
    const std::string component = raw_timestamp.substr(start, length);
    if (!std::all_of(component.begin(), component.end(),
                     [](unsigned char ch) { return std::isdigit(ch); })) {
        throw std::runtime_error("invalid");
    }
    return std::stoi(component);
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

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp) {
    const std::string timestamp = trim(raw_timestamp);
    if (timestamp.size() != 19 || (timestamp[4] != '-' && timestamp[4] != '/') ||
        timestamp[7] != timestamp[4] || timestamp[10] != ' ' || timestamp[13] != ':' ||
        timestamp[16] != ':') {
        throw std::runtime_error("invalid");
    }

    const int year = parse_int_component(timestamp, 0, 4);
    const int month = parse_int_component(timestamp, 5, 2);
    const int day = parse_int_component(timestamp, 8, 2);
    const int hour = parse_int_component(timestamp, 11, 2);
    const int minute = parse_int_component(timestamp, 14, 2);
    const int second = parse_int_component(timestamp, 17, 2);

    const std::chrono::year_month_day calendar_date{
        std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
        std::chrono::day{static_cast<unsigned>(day)}};
    if (!calendar_date.ok()) {
        throw std::runtime_error("invalid");
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        throw std::runtime_error("invalid");
    }

    const std::chrono::sys_seconds time_point =
        std::chrono::sys_days{calendar_date} + std::chrono::hours{hour} +
        std::chrono::minutes{minute} + std::chrono::seconds{second};
    return time_point.time_since_epoch().count();
}

std::string format_timestamp_seconds(std::int64_t seconds_since_epoch,
                                     char date_separator) {
    if (date_separator != '-' && date_separator != '/') {
        throw std::runtime_error("invalid");
    }

    const std::chrono::sys_seconds time_point{std::chrono::seconds{seconds_since_epoch}};
    const std::chrono::sys_days date = std::chrono::floor<std::chrono::days>(time_point);
    const std::chrono::year_month_day calendar_date{date};
    const std::chrono::seconds seconds_of_day = time_point - date;
    const std::chrono::hours hours =
        std::chrono::duration_cast<std::chrono::hours>(seconds_of_day);
    const std::chrono::minutes minutes =
        std::chrono::duration_cast<std::chrono::minutes>(seconds_of_day - hours);
    const std::chrono::seconds seconds = seconds_of_day - hours - minutes;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << static_cast<int>(calendar_date.year())
           << date_separator << std::setw(2) << static_cast<unsigned>(calendar_date.month())
           << date_separator << std::setw(2) << static_cast<unsigned>(calendar_date.day())
           << " " << std::setw(2) << hours.count() << ":" << std::setw(2) << minutes.count()
           << ":" << std::setw(2) << seconds.count();
    return output.str();
}

} // namespace miaow::io
