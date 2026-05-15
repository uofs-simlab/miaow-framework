#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <caf/expected.hpp>
#include <caf/json.hpp>

#include "miaow.hpp"

namespace miaow::io {

std::string trim(std::string value);

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

inline void write_json_string(std::ostream& output, const std::string& value) {
    output << '"';
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            output << '\\' << ch;
        } else if (ch == '\n') {
            output << "\\n";
        } else if (ch == '\r') {
            output << "\\r";
        } else if (ch == '\t') {
            output << "\\t";
        } else {
            output << ch;
        }
    }
    output << '"';
}

inline void write_indent(std::ostream& output, int indent) {
    for (int i = 0; i < indent; ++i) {
        output << ' ';
    }
}

inline void write_config_json(std::ostream& output,
                              const caf::config_value& value,
                              int indent);

inline void write_dictionary_json(
    std::ostream& output,
    const caf::config_value::dictionary& dictionary,
    int indent) {
    output << "{";
    if (!dictionary.empty()) {
        output << "\n";
    }

    std::size_t index = 0;
    for (const std::pair<const std::string, caf::config_value>& entry :
         dictionary) {
        write_indent(output, indent + 2);
        write_json_string(output, entry.first);
        output << ": ";
        write_config_json(output, entry.second, indent + 2);
        ++index;
        output << (index == dictionary.size() ? "\n" : ",\n");
    }

    if (!dictionary.empty()) {
        write_indent(output, indent);
    }
    output << "}";
}

inline void write_list_json(std::ostream& output,
                            const caf::config_value::list& list,
                            int indent) {
    output << "[";
    if (!list.empty()) {
        output << "\n";
    }

    for (std::size_t index = 0; index < list.size(); ++index) {
        write_indent(output, indent + 2);
        write_config_json(output, list.at(index), indent + 2);
        output << (index + 1 == list.size() ? "\n" : ",\n");
    }

    if (!list.empty()) {
        write_indent(output, indent);
    }
    output << "]";
}

inline void write_config_json(std::ostream& output,
                              const caf::config_value& value,
                              int indent) {
    if (const caf::config_value::dictionary* dictionary =
            caf::get_if<caf::config_value::dictionary>(&value)) {
        write_dictionary_json(output, *dictionary, indent);
    } else if (const caf::config_value::list* list =
                   caf::get_if<caf::config_value::list>(&value)) {
        write_list_json(output, *list, indent);
    } else if (const std::string* string_value =
                   caf::get_if<std::string>(&value)) {
        write_json_string(output, *string_value);
    } else if (const caf::config_value::integer* integer_value =
                   caf::get_if<caf::config_value::integer>(&value)) {
        output << *integer_value;
    } else if (const caf::config_value::real* real_value =
                   caf::get_if<caf::config_value::real>(&value)) {
        output << *real_value;
    } else if (const caf::config_value::boolean* boolean_value =
                   caf::get_if<caf::config_value::boolean>(&value)) {
        output << (*boolean_value ? "true" : "false");
    } else {
        output << "null";
    }
}

inline void write_config_json_file(
    const std::filesystem::path& path,
    const caf::config_value::dictionary& dictionary) {
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open JSON file '" +
                                 path.string() + "'");
    }
    write_dictionary_json(output, dictionary, 0);
    output << "\n";
}

// Throw validation errors with the caller's domain prefix
[[noreturn]] inline void throw_config_error(std::string_view error_prefix,
                                            const std::string& message) {
    throw std::runtime_error(std::string(error_prefix) + message);
}

// Required JSON objects are copied out after type validation
inline caf::json_object require_json_object(const caf::json_value& value,
                                            const std::string& context,
                                            std::string_view error_prefix) {
    if (!value.is_object()) {
        throw_config_error(error_prefix, context + " must be a JSON object");
    }
    return value.to_object();
}

// Optional JSON strings use the fallback when missing or blank
inline std::string optional_json_string(const caf::json_object& object,
                                        const std::string& key,
                                        const std::string& fallback,
                                        std::string_view error_prefix) {
    const caf::json_value value = object.value(key);
    if (value.is_undefined()) {
        return fallback;
    }
    if (!value.is_string()) {
        throw_config_error(error_prefix, "key '" + key + "' must be a string");
    }
    const std::string parsed = trim(std::string(value.to_string()));
    return parsed.empty() ? fallback : parsed;
}

// Read a JSON file and require an object at the root
inline caf::json_object read_json_object_file(
    const std::filesystem::path& path,
    const std::string& context,
    const std::string& parse_error_label,
    std::string_view error_prefix) {
    const caf::expected<caf::json_value> parsed =
        caf::json_value::parse_file(path.string());
    if (!parsed) {
        throw_config_error(
            error_prefix,
            parse_error_label + " '" + path.string() + "'");
    }
    return require_json_object(*parsed, context, error_prefix);
}

// JSON string maps require object entries with non-empty string values
inline std::map<std::string, std::string> parse_json_string_map(
    const caf::json_object& object,
    const std::string& key,
    std::string_view error_prefix) {
    const caf::json_value map_value = object.value(key);
    if (map_value.is_undefined()) {
        return {};
    }

    const std::string map_context = "key '" + key + "'";
    const caf::json_object map_object =
        require_json_object(map_value, map_context, error_prefix);

    std::map<std::string, std::string> values;
    for (caf::json_object::const_iterator map_it = map_object.begin();
         map_it != map_object.end();
         ++map_it) {
        const std::string map_key = trim(std::string(map_it.key()));
        const caf::json_value map_entry_value = map_it.value();
        if (map_key.empty() || !map_entry_value.is_string()) {
            throw_config_error(
                error_prefix,
                map_context + " entries must map non-empty strings to strings");
        }

        const std::string map_entry =
            trim(std::string(map_entry_value.to_string()));
        if (map_entry.empty()) {
            throw_config_error(
                error_prefix,
                map_context + " entries must map to non-empty strings");
        }
        values.emplace(map_key, map_entry);
    }
    return values;
}

// Required config dictionaries preserve the caller's context
inline const caf::config_value::dictionary& require_config_dictionary(
    const caf::config_value::dictionary& object,
    const std::string& key,
    const std::string& context) {
    const caf::config_value::dictionary::const_iterator entry = object.find(key);
    if (entry == object.end()) {
        throw std::runtime_error(context + " is missing key '" + key + "'");
    }
    const caf::config_value::dictionary* value =
        caf::get_if<caf::config_value::dictionary>(&entry->second);
    if (value == nullptr) {
        throw std::runtime_error(
            context + " key '" + key + "' must be a dictionary");
    }
    return *value;
}

// Required config strings must be present and non-empty
inline const std::string& require_config_string(
    const caf::config_value::dictionary& object,
    const std::string& key,
    const std::string& context) {
    const caf::config_value::dictionary::const_iterator entry = object.find(key);
    if (entry == object.end()) {
        throw std::runtime_error(context + " is missing key '" + key + "'");
    }
    const std::string* value = caf::get_if<std::string>(&entry->second);
    if (value == nullptr || value->empty()) {
        throw std::runtime_error(
            context + " key '" + key + "' must be a non-empty string");
    }
    return *value;
}

// Config string maps use CAF dictionaries with string values
inline std::map<std::string, std::string> parse_config_string_map(
    const caf::config_value::dictionary& object,
    const std::string& key,
    const std::string& context) {
    const caf::config_value::dictionary::const_iterator map_it =
        object.find(key);
    if (map_it == object.end()) {
        return {};
    }
    const caf::config_value::dictionary* map_value =
        caf::get_if<caf::config_value::dictionary>(&map_it->second);
    if (map_value == nullptr) {
        throw std::runtime_error(
            context + " key '" + key + "' must be a dictionary");
    }

    std::map<std::string, std::string> values;
    for (const std::pair<const std::string, caf::config_value>& map_entry :
         *map_value) {
        const std::string* value =
            caf::get_if<std::string>(&map_entry.second);
        if (value == nullptr) {
            throw std::runtime_error(
                context + " " + key + " entry '" + map_entry.first +
                "' must be a string");
        }
        values.emplace(map_entry.first, *value);
    }
    return values;
}

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
