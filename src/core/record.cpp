#include "internal.hpp"

#include <caf/json_array.hpp>
#include <caf/json_builder.hpp>
#include <caf/json_object.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace miaow::core {
namespace {

// Copy typed values without passing numbers through CAF's text formatter.
// Preserve field order, type annotations, and duration strings from the CAF builder.
nlohmann::ordered_json to_json_tree(const caf::json_value& value) {
    if (value.is_object()) {
        auto object = nlohmann::ordered_json::object();
        for (const auto& [key, item] : value.to_object()) {
            object[std::string(key)] = to_json_tree(item);
        }
        return object;
    }
    if (value.is_array()) {
        auto array = nlohmann::ordered_json::array();
        for (const auto& item : value.to_array()) {
            array.push_back(to_json_tree(item));
        }
        return array;
    }
    if (value.is_double()) {
        return value.to_double();
    }
    if (value.is_integer()) {
        return value.to_integer();
    }
    if (value.is_unsigned()) {
        return value.to_unsigned();
    }
    if (value.is_bool()) {
        return value.to_bool();
    }
    if (value.is_string()) {
        return std::string(value.to_string());
    }
    return nullptr;
}

} // namespace

void write_job_record(const std::filesystem::path& path, const JobRecord& record) {
    caf::json_builder builder;
    builder.skip_object_type_annotation(true);
    builder.skip_empty_fields(false);
    if (!builder.apply(record)) {
        throw std::runtime_error("failed to serialize JSON file '" + path.string() +
                                 "': " + caf::to_string(builder.get_error()));
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" + path.string() + "'");
    }
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << to_json_tree(builder.seal()).dump(2) << '\n';
    output.close();
}

} // namespace miaow::core
