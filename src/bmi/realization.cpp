#include "miaow_bmi.hpp"

#include "miaow_io.hpp"
#include "io/internal.hpp"

#include <filesystem>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <caf/expected.hpp>
#include <caf/config_value.hpp>
#include <caf/json.hpp>

namespace miaow {

namespace {

constexpr std::string_view realization_config_error_prefix =
    "Realization config error: ";
constexpr std::string_view realization_path_error_prefix =
    "Realization path error: ";

// Config errors describe malformed realization content
[[noreturn]] void throw_realization_config_error(const std::string& message) {
    throw std::runtime_error(
        std::string(realization_config_error_prefix) + message);
}

// Path errors describe missing or unusable files referenced by valid JSON
[[noreturn]] void throw_realization_path_error(const std::string& message) {
    throw std::runtime_error(
        std::string(realization_path_error_prefix) + message);
}

// Required objects are copied out of CAF JSON values after type validation
caf::json_object require_object_value(const caf::json_value& value,
                                      const std::string& context) {
    if (!value.is_object()) {
        throw_realization_config_error(context + " must be a JSON object");
    }
    return value.to_object();
}

// Required members preserve the caller's context in missing-key errors
caf::json_value require_member(const caf::json_object& object,
                               const std::string& key,
                               const std::string& context) {
    const caf::json_value member = object.value(key);
    if (member.is_undefined()) {
        throw_realization_config_error(context + " is missing key '" + key + "'");
    }
    return member;
}

// Required strings are trimmed before they enter the parsed realization
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

// File paths in realizations are resolved against the realization directory
std::filesystem::path resolve_path(const std::filesystem::path& base_directory,
                                   const std::string& raw_path) {
    std::filesystem::path resolved_path = std::filesystem::path(raw_path);
    if (resolved_path.is_relative()) {
        resolved_path = base_directory / resolved_path;
    }
    return std::filesystem::absolute(resolved_path).lexically_normal();
}

// Required file paths must point to readable regular files
std::string require_existing_file(const std::filesystem::path& path,
                                  const std::string& context) {
    if (!std::filesystem::exists(path)) {
        throw_realization_path_error(
            context + " path '" + path.string() + "' does not exist");
    }
    if (!std::filesystem::is_regular_file(path)) {
        throw_realization_path_error(
            context + " path '" + path.string() + "' is not a regular file");
    }
    return path.string();
}

// Required directory paths must point to readable directories
std::filesystem::path require_existing_directory(const std::filesystem::path& path,
                                                 const std::string& context) {
    if (!std::filesystem::exists(path)) {
        throw_realization_path_error(
            context + " path '" + path.string() + "' does not exist");
    }
    if (!std::filesystem::is_directory(path)) {
        throw_realization_path_error(
            context + " path '" + path.string() + "' is not a directory");
    }
    return path;
}

// CAF owns the JSON parser used by all realization configuration reads
caf::json_object parse_json_file(const std::filesystem::path& path,
                                 const std::string& context) {
    const caf::expected<caf::json_value> parsed =
        caf::json_value::parse_file(path.string());
    if (!parsed) {
        throw_realization_config_error(
            "unable to parse JSON file '" + path.string() + "'");
    }
    return require_object_value(*parsed, context);
}

// Variable maps bind BMI input names to forcing CSV headers
std::map<std::string, std::string> parse_variables_names_map(
    const caf::json_object& params_object,
    const std::string& context) {
    const caf::json_value map_value = params_object.value("variables_names_map");
    if (map_value.is_undefined()) {
        return {};
    }

    const std::string map_context = context + " key 'variables_names_map'";
    const caf::json_object map_object =
        require_object_value(map_value, map_context);

    std::map<std::string, std::string> variables_names_map;
    for (caf::json_object::const_iterator map_it = map_object.begin();
         map_it != map_object.end();
         ++map_it) {
        const std::string bmi_input = io::trim(std::string(map_it.key()));
        const caf::json_value csv_header_value = map_it.value();
        if (bmi_input.empty() || !csv_header_value.is_string()) {
            throw_realization_config_error(
                map_context + " entries must map non-empty strings to strings");
        }

        const std::string csv_header =
            io::trim(std::string(csv_header_value.to_string()));
        if (csv_header.empty()) {
            throw_realization_config_error(
                map_context + " entries must map to non-empty strings");
        }
        variables_names_map.emplace(bmi_input, csv_header);
    }
    return variables_names_map;
}

// Catchment IDs come from GeoJSON feature IDs or SYMFLUENCE id properties
std::vector<std::string> read_catchment_ids(const std::filesystem::path& path) {
    const caf::json_object root = parse_json_file(path, "catchments root");
    const caf::json_value features_value =
        require_member(root, "features", "catchments root");
    if (!features_value.is_array()) {
        throw_realization_config_error(
            "catchments root key 'features' must be a JSON array");
    }

    std::vector<std::string> catchment_ids;
    const caf::json_array features = features_value.to_array();
    catchment_ids.reserve(features.size());
    for (const caf::json_value& feature_value : features) {
        const caf::json_object feature =
            require_object_value(feature_value, "catchments feature");
        caf::json_value id_value = feature.value("id");
        if (id_value.is_undefined()) {
            const caf::json_object properties = require_object_value(
                require_member(feature, "properties", "catchments feature"),
                "catchments feature key 'properties'");
            id_value = require_member(
                properties,
                "id",
                "catchments feature key 'properties'");
        }
        if (!id_value.is_string()) {
            throw_realization_config_error("catchment id must be a string");
        }

        const std::string catchment_id =
            io::trim(std::string(id_value.to_string()));
        if (catchment_id.empty()) {
            throw_realization_config_error("catchment id must be non-empty");
        }
        catchment_ids.push_back(catchment_id);
    }

    if (catchment_ids.empty()) {
        throw_realization_config_error("catchments file must contain features");
    }
    return catchment_ids;
}

// SYMFLUENCE templates identify per-catchment files with {{id}}
std::string replace_id_template(const std::string& template_text,
                                const std::string& catchment_id,
                                const std::string& context) {
    const std::string placeholder = "{{id}}";
    if (template_text.find(placeholder) == std::string::npos) {
        throw_realization_config_error(context + " must contain '{{id}}'");
    }

    std::string result = template_text;
    std::string::size_type position = 0;
    while ((position = result.find(placeholder, position)) != std::string::npos) {
        result.replace(position, placeholder.size(), catchment_id);
        position += catchment_id.size();
    }
    return result;
}

// Forcing files are selected with the SYMFLUENCE CsvPerFeature regex
std::string match_forcing_file(const std::filesystem::path& forcing_directory,
                               const std::string& pattern_text,
                               const std::string& catchment_id) {
    std::regex pattern;
    try {
        pattern = std::regex(pattern_text);
    } catch (const std::regex_error&) {
        throw_realization_config_error("forcing file_pattern is not a valid regex");
    }

    std::vector<std::filesystem::path> matches;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(forcing_directory)) {
        const std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() && std::regex_match(filename, pattern)) {
            matches.push_back(entry.path());
        }
    }

    if (matches.size() != 1) {
        throw_realization_path_error(
            "expected one forcing file for catchment '" + catchment_id +
            "', found " + std::to_string(matches.size()));
    }
    return matches.front().string();
}

// The supported SYMFLUENCE realization contains one bmi_multi Fortran module
caf::json_object parse_fortran_module_params(const caf::json_object& root) {
    const caf::json_object global_object = require_object_value(
        require_member(root, "global", "root"),
        "root key 'global'");
    const caf::json_value formulations_value =
        require_member(global_object, "formulations", "root key 'global'");
    if (!formulations_value.is_array()) {
        throw_realization_config_error(
            "root key 'global' key 'formulations' must be a JSON array");
    }

    const caf::json_array formulations = formulations_value.to_array();
    if (formulations.size() != 1) {
        throw_realization_config_error("formulations must contain one entry");
    }

    const caf::json_object formulation =
        require_object_value(*formulations.begin(), "formulations entry 0");
    const std::string formulation_name =
        require_non_empty_string(formulation, "name", "formulations entry 0");
    if (formulation_name != "bmi_multi") {
        throw_realization_config_error(
            "unsupported formulation '" + formulation_name + "'");
    }

    const caf::json_object formulation_params = require_object_value(
        require_member(formulation, "params", "formulations entry 0"),
        "formulations entry 0 key 'params'");
    const caf::json_value modules_value =
        require_member(formulation_params, "modules", "bmi_multi params");
    if (!modules_value.is_array()) {
        throw_realization_config_error(
            "bmi_multi params key 'modules' must be a JSON array");
    }

    const caf::json_array modules = modules_value.to_array();
    if (modules.size() != 1) {
        throw_realization_config_error("bmi_multi modules must contain one entry");
    }

    const caf::json_object module =
        require_object_value(*modules.begin(), "bmi_multi modules entry 0");
    const std::string module_name =
        require_non_empty_string(module, "name", "bmi_multi modules entry 0");
    if (module_name != "bmi_fortran") {
        throw_realization_config_error("unsupported module '" + module_name + "'");
    }

    return require_object_value(
        require_member(module, "params", "bmi_multi modules entry 0"),
        "bmi_multi modules entry 0 key 'params'");
}

// The supported SYMFLUENCE realization uses CsvPerFeature forcing
caf::json_object parse_forcing(const caf::json_object& root) {
    const caf::json_object global_object = require_object_value(
        require_member(root, "global", "root"),
        "root key 'global'");
    const caf::json_object forcing = require_object_value(
        require_member(global_object, "forcing", "root key 'global'"),
        "root key 'global' key 'forcing'");
    const std::string provider =
        require_non_empty_string(forcing, "provider", "global forcing");
    if (provider != "CsvPerFeature") {
        throw_realization_config_error(
            "unsupported forcing provider '" + provider + "'");
    }
    return forcing;
}

SimulationUnitId register_unit(SimulationUnitRegistry& registry,
                               std::map<std::string, SimulationUnitId>& unit_ids,
                               const std::string& library_file,
                               BmiExecutionMode execution_mode) {
    const std::map<std::string, SimulationUnitId>::const_iterator existing_unit =
        unit_ids.find(library_file);
    if (existing_unit != unit_ids.end()) {
        return existing_unit->second;
    }

    // Realization-driven BMI execution currently supports Fortran modules
    const SimulationUnitId unit_id =
        registry.add<BmiSimulationUnit<DynamicFortranBmi>>(
            library_file,
            execution_mode);
    unit_ids.emplace(library_file, unit_id);
    return unit_id;
}

} // namespace

RealizationConfig::RealizationConfig(const std::string& realization_path) {
    if (io::trim(realization_path).empty()) {
        throw_realization_path_error("empty realization path");
    }
    const std::filesystem::path normalized_path =
        std::filesystem::absolute(std::filesystem::path(realization_path))
            .lexically_normal();
    require_existing_file(normalized_path, "realization");

    const std::filesystem::path base_directory = normalized_path.parent_path();
    const caf::json_object root = parse_json_file(normalized_path, "root");
    const caf::json_object module_params = parse_fortran_module_params(root);
    const caf::json_object forcing = parse_forcing(root);

    const std::string library_path_text =
        require_non_empty_string(module_params, "library_file", "module params");
    const std::string init_template =
        require_non_empty_string(module_params, "init_config", "module params");
    const std::map<std::string, std::string> variables_names_map =
        parse_variables_names_map(module_params, "module params");

    const std::string forcing_path_text =
        require_non_empty_string(forcing, "path", "global forcing");
    const std::string forcing_pattern_template =
        require_non_empty_string(forcing, "file_pattern", "global forcing");

    const std::filesystem::path library_path =
        resolve_path(base_directory, library_path_text);
    const std::string library_file =
        require_existing_file(library_path, "module library_file");
    const std::filesystem::path forcing_directory = require_existing_directory(
        resolve_path(base_directory, forcing_path_text),
        "global forcing");
    const std::filesystem::path catchments_path =
        base_directory / "catchments.geojson";
    require_existing_file(catchments_path, "catchments");
    const std::vector<std::string> catchment_ids =
        read_catchment_ids(catchments_path);

    const caf::json_value output_root_value = root.value("output_root");
    if (!output_root_value.is_undefined()) {
        if (!output_root_value.is_string()) {
            throw_realization_config_error("root key 'output_root' must be a string");
        }
        const std::string output_root =
            io::trim(std::string(output_root_value.to_string()));
        if (!output_root.empty()) {
            output_root_ = resolve_path(base_directory, output_root).string();
        }
    }

    features_.reserve(catchment_ids.size());
    for (const std::string& catchment_id : catchment_ids) {
        const std::string init_config = replace_id_template(
            init_template,
            catchment_id,
            "module init_config");
        const std::string forcing_pattern = replace_id_template(
            forcing_pattern_template,
            catchment_id,
            "global forcing file_pattern");

        RealizationFeature feature;
        feature.feature_id_ = catchment_id;
        feature.library_file_ = library_file;
        feature.init_config_path_ = require_existing_file(
            resolve_path(base_directory, init_config),
            "module init_config");
        feature.forcing_file_ =
            match_forcing_file(forcing_directory, forcing_pattern, catchment_id);
        feature.variables_names_map_ = variables_names_map;
        features_.push_back(feature);
    }
}

std::vector<Task> build_bmi_realization_tasks(const RealizationConfig& realization,
                                              SimulationUnitRegistry& registry,
                                              BmiExecutionMode execution_mode) {
    std::map<std::string, SimulationUnitId> unit_ids;

    // Register all dynamic BMI units before the runtime is constructed
    for (const RealizationFeature& feature : realization.features_) {
        register_unit(registry, unit_ids, feature.library_file_, execution_mode);
    }

    // Build one task for each feature in realization order
    std::vector<Task> tasks;
    tasks.reserve(realization.features_.size());
    for (const RealizationFeature& feature : realization.features_) {
        const SimulationUnitId unit_id = unit_ids.at(feature.library_file_);

        // MIAOW-managed CSV forcing is passed as the second BMI task argument
        Task task(
            unit_id,
            std::vector<caf::config_value>{
                caf::config_value{feature.init_config_path_},
                make_forcing_config(
                    feature.forcing_file_,
                    feature.variables_names_map_),
        });

        // Feature metadata provides the hydrofabric join key for map outputs
        task.metadata_["feature_id"] = feature.feature_id_;
        task.metadata_["feature_type"] = "catchment";
        task.metadata_["hydrofabric_layer"] = "divides";
        task.metadata_["hydrofabric_id_field"] = "id";
        task.metadata_["config_path"] = feature.init_config_path_;
        task.metadata_["forcing_file"] = feature.forcing_file_;
        tasks.push_back(task);
    }

    return tasks;
}

} // namespace miaow
