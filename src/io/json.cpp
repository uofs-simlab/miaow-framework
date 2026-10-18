#include "internal.hpp"

#include <boost/json.hpp>

#include <cstddef>
#include <cmath>
#include <ostream>

namespace miaow::io {
namespace {

// CAF builds the JSON tree and type annotations; Boost formats doubles with enough
// digits to round-trip. Keep the same two-space indentation for human-readable files.
void write_json(std::ostream& output, const caf::json_value& value, std::size_t indent) {
    if (value.is_object()) {
        output << '{';
        bool first = true;
        for (const auto& [key, item] : value.to_object()) {
            output << (first ? "\n" : ",\n") << std::string(indent + 2, ' ')
                   << boost::json::serialize(boost::json::value{key}) << ": ";
            write_json(output, item, indent + 2);
            first = false;
        }
        if (!first) {
            output << '\n' << std::string(indent, ' ');
        }
        output << '}';
    } else if (value.is_array()) {
        output << '[';
        bool first = true;
        for (const auto& item : value.to_array()) {
            output << (first ? "\n" : ",\n") << std::string(indent + 2, ' ');
            write_json(output, item, indent + 2);
            first = false;
        }
        if (!first) {
            output << '\n' << std::string(indent, ' ');
        }
        output << ']';
    } else if (value.is_double()) {
        const double number = value.to_double();
        // CAF 1.1 rejects exponent notation for zero, including Boost's -0E0.
        if (number == 0.0) {
            output << (std::signbit(number) ? "-0.0" : "0.0");
        } else {
            output << boost::json::serialize(boost::json::value{number});
        }
    } else {
        std::string text;
        value.print_to(text);
        output << text;
    }
}

} // namespace

void write_json_file(const std::filesystem::path& path, const caf::json_value& value) {
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" + path.string() + "'");
    }
    output.exceptions(std::ios::failbit | std::ios::badbit);
    write_json(output, value, 0);
    output << '\n';
    output.close();
}

} // namespace miaow::io
