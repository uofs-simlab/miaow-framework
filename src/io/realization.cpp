#include "miaow_io.hpp"

#include "io/internal.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <caf/expected.hpp>
#include <caf/json.hpp>

namespace miaow {

namespace {

constexpr std::string_view realization_config_error_prefix =
    "Realization config error: ";
constexpr std::string_view realization_path_error_prefix =
    "Realization path error: ";

[[noreturn]] void throw_realization_config_error(const std::string& message) {
    throw std::runtime_error(
        std::string(realization_config_error_prefix) + message);
}

[[noreturn]] void throw_realization_path_error(const std::string& message) {
    throw std::runtime_error(
        std::string(realization_path_error_prefix) + message);
}

std::string make_subdomain_context(const std::string& subdomain_id) {
    return "root key 'subdomains' entry '" + subdomain_id + "'";
}

std::string make_subdomain_field_context(const std::string& subdomain_id,
                                         std::string_view field_name) {
    return make_subdomain_context(subdomain_id) + " key '" +
           std::string(field_name) + "'";
}

std::filesystem::path normalize_realization_path(
    const std::string& realization_path) {
    if (io::trim(realization_path).empty()) {
        throw_realization_path_error("empty realization path");
    }

    const std::filesystem::path normalized_path =
        std::filesystem::absolute(std::filesystem::path(realization_path))
            .lexically_normal();
    if (!std::filesystem::exists(normalized_path)) {
        throw_realization_path_error(
            "file '" + normalized_path.string() + "' does not exist");
    }
    if (!std::filesystem::is_regular_file(normalized_path)) {
        throw_realization_path_error(
            "path '" + normalized_path.string() + "' is not a regular file");
    }

    return normalized_path;
}

caf::json_object require_object_value(const caf::json_value& value,
                                      const std::string& context) {
    if (!value.is_object()) {
        throw_realization_config_error(context + " must be a JSON object");
    }
    return value.to_object();
}

caf::json_value require_member(const caf::json_object& object,
                               const std::string& key,
                               const std::string& context) {
    const caf::json_value member = object.value(key);
    if (member.is_undefined()) {
        throw_realization_config_error(context + " is missing key '" + key + "'");
    }
    return member;
}

std::string require_non_empty_string(const caf::json_object& object,
                                     const std::string& key,
                                     const std::string& context) {
    const caf::json_value value = require_member(object, key, context);
    if (!value.is_string()) {
        throw_realization_config_error(
            context + " key '" + key + "' must be a string");
    }

    const std::string parsed = io::trim(std::string(value.to_string()));
    if (parsed.empty()) {
        throw_realization_config_error(
            context + " key '" + key + "' must be a non-empty string");
    }
    return parsed;
}

std::filesystem::path resolve_input_path(const std::filesystem::path& base_directory,
                                         const std::string& raw_path,
                                         const std::string& field_name,
                                         const std::string& subdomain_id) {
    // Realization-relative inputs stay portable when examples move together
    std::filesystem::path resolved_path = std::filesystem::path(raw_path);
    if (resolved_path.is_relative()) {
        resolved_path = base_directory / resolved_path;
    }
    resolved_path = std::filesystem::absolute(resolved_path).lexically_normal();

    if (!std::filesystem::exists(resolved_path)) {
        throw_realization_path_error(
            "subdomain '" + subdomain_id + "' field '" + field_name +
            "' path '" + resolved_path.string() + "' does not exist");
    }
    if (!std::filesystem::is_regular_file(resolved_path)) {
        throw_realization_path_error(
            "subdomain '" + subdomain_id + "' field '" + field_name +
            "' path '" + resolved_path.string() + "' is not a regular file");
    }

    return resolved_path;
}

std::string require_resolved_input_path(const caf::json_object& object,
                                        const std::filesystem::path& base_directory,
                                        const std::string& field_name,
                                        const std::string& subdomain_id) {
    const std::string field_context =
        make_subdomain_context(subdomain_id);
    const std::string raw_path =
        require_non_empty_string(object, field_name, field_context);
    return resolve_input_path(base_directory, raw_path, field_name, subdomain_id)
        .string();
}

std::optional<std::string> parse_optional_resolved_input_path(
    const caf::json_object& object,
    const std::filesystem::path& base_directory,
    const std::string& field_name,
    const std::string& subdomain_id) {
    const caf::json_value value = object.value(field_name);
    if (value.is_undefined()) {
        return std::nullopt;
    }

    const std::string field_context =
        make_subdomain_field_context(subdomain_id, field_name);
    if (!value.is_string()) {
        throw_realization_config_error(field_context + " must be a string");
    }

    const std::string raw_path = io::trim(std::string(value.to_string()));
    if (raw_path.empty()) {
        throw_realization_config_error(
            field_context + " must be a non-empty string");
    }

    // Optional forcing paths follow the same realization-relative resolution
    return resolve_input_path(base_directory, raw_path, field_name, subdomain_id)
        .string();
}

std::map<std::string, std::string> parse_variables_names_map(
    const caf::json_object& subdomain_object,
    const std::string& subdomain_id) {
    // The map is optional, but any provided entry must be fully typed and named
    const caf::json_value variables_names_map_value =
        subdomain_object.value("variables_names_map");
    if (variables_names_map_value.is_undefined()) {
        return {};
    }

    const std::string map_context =
        make_subdomain_field_context(subdomain_id, "variables_names_map");
    const caf::json_object variables_names_map_object = require_object_value(
        variables_names_map_value, map_context);
    if (variables_names_map_object.empty()) {
        throw_realization_config_error(
            map_context + " must not be empty");
    }

    std::map<std::string, std::string> variables_names_map;
    for (const auto [raw_bmi_input, csv_header_value] : variables_names_map_object) {
        const std::string bmi_input = io::trim(std::string(raw_bmi_input));
        if (bmi_input.empty()) {
            throw_realization_config_error(
                map_context + " contains an empty BMI input name");
        }
        if (!csv_header_value.is_string()) {
            throw_realization_config_error(
                map_context + " entry '" + bmi_input + "' must be a string");
        }

        const std::string csv_header =
            io::trim(std::string(csv_header_value.to_string()));
        if (csv_header.empty()) {
            throw_realization_config_error(
                map_context + " entry '" + bmi_input +
                "' must be a non-empty string");
        }

        variables_names_map.emplace(bmi_input, csv_header);
    }
    return variables_names_map;
}

std::vector<RealizationSubdomain> parse_subdomains(
    const caf::json_object& root,
    const std::filesystem::path& base_directory) {
    const caf::json_value subdomains_value =
        require_member(root, "subdomains", "root");
    const caf::json_object subdomains_object =
        require_object_value(subdomains_value, "root key 'subdomains'");

    if (subdomains_object.empty()) {
        throw_realization_config_error("root key 'subdomains' must not be empty");
    }

    std::vector<RealizationSubdomain> subdomains;
    subdomains.reserve(subdomains_object.size());
    for (const auto [raw_subdomain_id, subdomain_value] : subdomains_object) {
        const std::string subdomain_id = io::trim(std::string(raw_subdomain_id));
        if (subdomain_id.empty()) {
            throw_realization_config_error(
                "root key 'subdomains' contains an empty subdomain ID key");
        }

        const std::string subdomain_context =
            make_subdomain_context(subdomain_id);
        const caf::json_object subdomain_object = require_object_value(
            subdomain_value, subdomain_context);

        RealizationSubdomain subdomain;
        subdomain.subdomain_id_ = subdomain_id;
        subdomain.config_path_ = require_resolved_input_path(
            subdomain_object, base_directory, "config", subdomain_id);
        // Forcing metadata is optional and only affects MIAOW-managed forcing
        subdomain.forcing_file_ = parse_optional_resolved_input_path(
            subdomain_object, base_directory, "forcing_file", subdomain_id);
        subdomain.variables_names_map_ =
            parse_variables_names_map(subdomain_object, subdomain_id);
        if (!subdomain.forcing_file_.has_value() &&
            !subdomain.variables_names_map_.empty()) {
            // A map without a file leaves no source of values to apply
            throw_realization_config_error(
                make_subdomain_field_context(subdomain_id, "variables_names_map") +
                " requires key 'forcing_file'");
        }
        subdomains.push_back(std::move(subdomain));
    }

    return subdomains;
}

} // namespace

RealizationConfig read_realization_config(const std::string& realization_path) {
    const std::filesystem::path normalized_path =
        normalize_realization_path(realization_path);

    const caf::expected<caf::json_value> parsed =
        caf::json_value::parse_file(normalized_path.string());
    if (!parsed) {
        throw_realization_config_error(
            "unable to parse JSON file '" + normalized_path.string() + "'");
    }

    const caf::json_value root_value = *parsed;
    const caf::json_object root = require_object_value(root_value, "root");

    RealizationConfig realization_config;
    realization_config.realization_path_ = normalized_path.string();
    realization_config.subdomains_ =
        parse_subdomains(root, normalized_path.parent_path());
    return realization_config;
}

} // namespace miaow
