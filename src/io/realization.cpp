#include "io/realization.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <caf/expected.hpp>
#include <caf/json.hpp>

#include "io/utils.hpp"

namespace miaow::io {

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

std::filesystem::path normalize_realization_path(
    const std::string& realization_path) {
    if (trim(realization_path).empty()) {
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

    const std::string parsed = trim(std::string(value.to_string()));
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
        const std::string subdomain_id = trim(std::string(raw_subdomain_id));
        if (subdomain_id.empty()) {
            throw_realization_config_error(
                "root key 'subdomains' contains an empty subdomain ID key");
        }

        const caf::json_object subdomain_object = require_object_value(
            subdomain_value,
            "root key 'subdomains' entry '" + subdomain_id + "'");

        const std::string config_path = require_non_empty_string(
            subdomain_object, "config",
            "root key 'subdomains' entry '" + subdomain_id + "'");

        RealizationSubdomain subdomain;
        subdomain.subdomain_id_ = subdomain_id;
        subdomain.config_path_ =
            resolve_input_path(base_directory, config_path, "config", subdomain_id)
                .string();
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

} // namespace miaow::io
