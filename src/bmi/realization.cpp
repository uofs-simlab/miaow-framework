#include "miaow_bmi.hpp"

#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <filesystem>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include <caf/config_value.hpp>
#include <caf/json.hpp>

namespace miaow {

namespace {

// Config errors describe malformed realization content
[[noreturn]] void throw_realization_config_error(const std::string& message) {
    throw std::runtime_error(std::string("Realization config error: ") + message);
}

// Path errors describe missing or unusable files referenced by valid JSON
[[noreturn]] void throw_realization_path_error(const std::string& message) {
    throw std::runtime_error(std::string("Realization path error: ") + message);
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
        throw_realization_path_error(context + " path '" + path.string() +
                                     "' does not exist");
    }
    if (!std::filesystem::is_regular_file(path)) {
        throw_realization_path_error(context + " path '" + path.string() +
                                     "' is not a regular file");
    }
    return path.string();
}

// Required directory paths must point to readable directories
std::filesystem::path require_existing_directory(const std::filesystem::path& path,
                                                 const std::string& context) {
    if (!std::filesystem::exists(path)) {
        throw_realization_path_error(context + " path '" + path.string() +
                                     "' does not exist");
    }
    if (!std::filesystem::is_directory(path)) {
        throw_realization_path_error(context + " path '" + path.string() +
                                     "' is not a directory");
    }
    return path;
}

// CAF owns the JSON parser used by all realization configuration reads
caf::json_object parse_json_file(const std::filesystem::path& path,
                                 const std::string& context) {
    return io::read_json_object_file(path, context, "unable to parse JSON file",
                                     "Realization config error: ");
}

// Catchment IDs come from GeoJSON feature IDs or SYMFLUENCE id properties
std::vector<std::string> read_catchment_ids(const std::filesystem::path& path) {
    const caf::json_object root = parse_json_file(path, "catchments root");
    std::vector<std::string> catchment_ids;
    const caf::json_array features = root.value("features").to_array();
    catchment_ids.reserve(features.size());
    for (const caf::json_value& feature_value : features) {
        const caf::json_object feature = feature_value.to_object();
        caf::json_value id_value = feature.value("id");
        if (id_value.is_undefined()) {
            const caf::json_object properties = feature.value("properties").to_object();
            id_value = properties.value("id");
        }

        const std::string catchment_id = io::trim(std::string(id_value.to_string()));
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
        throw_realization_path_error("expected one forcing file for catchment '" +
                                     catchment_id + "', found " +
                                     std::to_string(matches.size()));
    }
    return matches.front().string();
}

// The supported SYMFLUENCE realization contains one bmi_multi Fortran module
bmi::BmiModuleConfig parse_fortran_module_config(const caf::json_object& global_object) {
    const caf::json_array formulations = global_object.value("formulations").to_array();
    if (formulations.size() != 1) {
        throw_realization_config_error("formulations must contain one entry");
    }

    const caf::json_object formulation = (*formulations.begin()).to_object();
    const std::string formulation_name =
        io::trim(std::string(formulation.value("name").to_string()));
    if (formulation_name != "bmi_multi") {
        throw_realization_config_error("unsupported formulation '" + formulation_name +
                                       "'");
    }

    const caf::json_object bmi_multi_params = formulation.value("params").to_object();
    const caf::json_array modules = bmi_multi_params.value("modules").to_array();
    if (modules.size() != 1) {
        throw_realization_config_error("bmi_multi modules must contain one entry");
    }

    bmi::BmiModuleConfig module_config((*modules.begin()).to_object(),
                                       "Realization config error: ");
    if (module_config.name_ != "bmi_fortran") {
        throw_realization_config_error("unsupported module '" + module_config.name_ + "'");
    }

    return module_config;
}

std::string
pipeline_config_feature_id(const caf::config_value::dictionary& pipeline_config) {
    const caf::config_value::dictionary::const_iterator feature_it =
        pipeline_config.find("feature");
    if (feature_it == pipeline_config.end()) {
        throw_realization_config_error("pipeline config is missing key 'feature'");
    }
    const caf::config_value::dictionary* feature =
        caf::get_if<caf::config_value::dictionary>(&feature_it->second);
    if (feature == nullptr) {
        throw_realization_config_error(
            "pipeline config key 'feature' must be a dictionary");
    }
    const caf::config_value::dictionary::const_iterator id_it = feature->find("id");
    if (id_it == feature->end()) {
        throw_realization_config_error("pipeline config feature is missing key 'id'");
    }
    const std::string* feature_id = caf::get_if<std::string>(&id_it->second);
    if (feature_id == nullptr || io::trim(*feature_id).empty()) {
        throw_realization_config_error(
            "pipeline config feature key 'id' must be a non-empty string");
    }
    return *feature_id;
}

} // namespace

// Parse a realization from a NextGen-style file into feature pipeline configs
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
    const caf::json_object global = root.value("global").to_object();
    bmi::BmiModuleConfig module_config = parse_fortran_module_config(global);
    const caf::json_object forcing = global.value("forcing").to_object();
    const std::string provider =
        io::trim(std::string(forcing.value("provider").to_string()));
    if (provider != "CsvPerFeature") {
        throw_realization_config_error("unsupported forcing provider '" + provider + "'");
    }

    const std::string init_template = module_config.config_file_;

    const std::string forcing_path_text =
        io::trim(std::string(forcing.value("path").to_string()));
    const std::string forcing_pattern_template =
        io::trim(std::string(forcing.value("file_pattern").to_string()));

    const std::filesystem::path library_path =
        resolve_path(base_directory, module_config.plugin_path_);
    module_config.plugin_path_ = require_existing_file(library_path, "module library_file");
    const std::filesystem::path forcing_directory =
        require_existing_directory(resolve_path(base_directory, forcing_path_text),
                                   "global forcing");
    const std::filesystem::path catchments_path = base_directory / "catchments.geojson";
    require_existing_file(catchments_path, "catchments");
    const std::vector<std::string> catchment_ids = read_catchment_ids(catchments_path);

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

    feature_pipeline_configs_.reserve(catchment_ids.size());
    for (const std::string& catchment_id : catchment_ids) {
        const std::string init_config =
            replace_id_template(init_template, catchment_id, "module init_config");
        const std::string forcing_pattern =
            replace_id_template(forcing_pattern_template, catchment_id,
                                "global forcing file_pattern");

        const std::string init_config_path =
            require_existing_file(resolve_path(base_directory, init_config),
                                  "module init_config");
        const std::string forcing_file =
            match_forcing_file(forcing_directory, forcing_pattern, catchment_id);

        bmi::BmiModuleConfig task_module_config = module_config;
        task_module_config.config_file_ = init_config_path;

        caf::config_value::dictionary feature;
        feature.insert("id", catchment_id);
        feature.insert("type", "catchment");
        feature.insert("hydrofabric_layer", "divides");
        feature.insert("hydrofabric_id_field", "id");

        caf::config_value::dictionary pipeline_config;
        pipeline_config.insert("feature", std::move(feature));
        pipeline_config.insert("global",
                               bmi::make_bmi_multi_global_config(task_module_config,
                                                                 forcing_file));
        feature_pipeline_configs_.push_back(std::move(pipeline_config));
    }
}

// Partition a realization into one MIAOW task per feature
std::vector<Task> build_bmi_realization_tasks(const RealizationConfig& realization,
                                              SimulationUnitRegistry& registry,
                                              BmiExecutionMode execution_mode) {
    // One pipeline unit handles all realization feature pipeline configs
    const SimulationUnitId unit_id =
        registry.add<BmiPipelineSimulationUnit>(execution_mode);

    // Build one task for each feature in realization order
    std::vector<Task> tasks;
    tasks.reserve(realization.feature_pipeline_configs_.size());
    for (const caf::config_value::dictionary& pipeline_config :
         realization.feature_pipeline_configs_) {
        const std::string feature_id = pipeline_config_feature_id(pipeline_config);

        // Feature metadata provides the hydrofabric join key for map outputs
        Task task(unit_id, caf::config_value{pipeline_config},
                  TaskMetadata{{"feature_id", feature_id},
                               {"feature_type", "catchment"},
                               {"hydrofabric_layer", "divides"},
                               {"hydrofabric_id_field", "id"}});
        tasks.push_back(task);
    }

    return tasks;
}

} // namespace miaow
