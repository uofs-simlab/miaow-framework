#include "miaow_bmi.hpp"

#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/json.hpp>

namespace miaow {

namespace json = boost::json;

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

// Feature IDs come from GeoJSON feature IDs or SYMFLUENCE id properties
std::vector<std::string> read_feature_ids(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw_realization_path_error("features path '" + path.string() +
                                     "' could not be opened");
    }
    const json::object root = json::parse(input).as_object();
    std::vector<std::string> feature_ids;
    const json::array& features = root.at("features").as_array();
    feature_ids.reserve(features.size());
    for (const json::value& feature_value : features) {
        const json::object& feature = feature_value.as_object();
        const json::value* id_value = feature.if_contains("id");
        if (id_value == nullptr) {
            const json::object& properties = feature.at("properties").as_object();
            id_value = properties.if_contains("id");
        }
        if (id_value == nullptr) {
            throw_realization_config_error("feature id must be present");
        }

        const std::string feature_id =
            io::trim(std::string(id_value->as_string()));
        if (feature_id.empty()) {
            throw_realization_config_error("feature id must be non-empty");
        }
        feature_ids.push_back(feature_id);
    }

    if (feature_ids.empty()) {
        throw_realization_config_error("feature collection must contain features");
    }
    return feature_ids;
}

// SYMFLUENCE templates identify per-feature files with {{id}}
std::string replace_id_template(const std::string& template_text,
                                const std::string& feature_id, const std::string& context) {
    const std::string placeholder = "{{id}}";
    if (template_text.find(placeholder) == std::string::npos) {
        throw_realization_config_error(context + " must contain '{{id}}'");
    }

    std::string result = template_text;
    std::string::size_type position = 0;
    while ((position = result.find(placeholder, position)) != std::string::npos) {
        result.replace(position, placeholder.size(), feature_id);
        position += feature_id.size();
    }
    return result;
}

// Forcing files are selected with the SYMFLUENCE CsvPerFeature regex
std::string match_forcing_file(const std::filesystem::path& forcing_directory,
                               const std::string& pattern_text,
                               const std::string& feature_id) {
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
        throw_realization_path_error("expected one forcing file for feature '" +
                                     feature_id + "', found " +
                                     std::to_string(matches.size()));
    }
    return matches.front().string();
}

// The supported realization contains one bmi_multi Fortran module
BmiModuleConfig parse_fortran_module_config(const json::object& global_object) {
    const json::array& formulations = global_object.at("formulations").as_array();
    if (formulations.size() != 1) {
        throw_realization_config_error("formulations must contain one entry");
    }

    const json::object& formulation = formulations.at(0).as_object();
    const std::string formulation_name =
        io::trim(std::string(formulation.at("name").as_string()));
    if (formulation_name != "bmi_multi") {
        throw_realization_config_error("unsupported formulation '" + formulation_name +
                                       "'");
    }

    const json::object& bmi_multi_params = formulation.at("params").as_object();
    const json::array& modules = bmi_multi_params.at("modules").as_array();
    if (modules.size() != 1) {
        throw_realization_config_error("bmi_multi modules must contain one entry");
    }

    BmiModuleConfig module_config = bmi::parse_bmi_module_config(
        modules.at(0).as_object(), "Realization config error: ");
    if (module_config.name_ != "bmi_fortran") {
        throw_realization_config_error("unsupported module '" + module_config.name_ + "'");
    }

    return module_config;
}

} // namespace

// Partition a global NextGen-style realization file into per-feature pipeline configs
PartitionedBmiRealization::PartitionedBmiRealization(const std::string& realization_path) {
    if (io::trim(realization_path).empty()) {
        throw_realization_path_error("empty realization path");
    }
    const std::filesystem::path normalized_path =
        std::filesystem::absolute(std::filesystem::path(realization_path))
            .lexically_normal();
    require_existing_file(normalized_path, "realization");

    const std::filesystem::path base_directory = normalized_path.parent_path();
    std::ifstream input(normalized_path);
    if (!input.is_open()) {
        throw_realization_path_error("realization path '" + normalized_path.string() +
                                     "' could not be opened");
    }
    const json::object root = json::parse(input).as_object();
    const json::object& global = root.at("global").as_object();
    BmiModuleConfig module_config = parse_fortran_module_config(global);
    const json::object& forcing = global.at("forcing").as_object();
    const std::string provider =
        io::trim(std::string(forcing.at("provider").as_string()));
    if (provider != "CsvPerFeature") {
        throw_realization_config_error("unsupported forcing provider '" + provider + "'");
    }

    const std::string init_template = module_config.config_file_;

    const std::string forcing_path_text =
        io::trim(std::string(forcing.at("path").as_string()));
    const std::string forcing_pattern_template =
        io::trim(std::string(forcing.at("file_pattern").as_string()));

    const std::filesystem::path library_path =
        resolve_path(base_directory, module_config.plugin_path_);
    module_config.plugin_path_ = require_existing_file(library_path, "module library_file");
    const std::filesystem::path forcing_directory =
        require_existing_directory(resolve_path(base_directory, forcing_path_text),
                                   "global forcing");
    const std::filesystem::path features_path = base_directory / "catchments.geojson";
    require_existing_file(features_path, "features");
    const std::vector<std::string> feature_ids = read_feature_ids(features_path);

    if (const json::value* output_root_value = root.if_contains("output_root")) {
        if (!output_root_value->is_string()) {
            throw_realization_config_error("root key 'output_root' must be a string");
        }
        const std::string output_root =
            io::trim(std::string(output_root_value->as_string()));
        if (!output_root.empty()) {
            output_root_ = resolve_path(base_directory, output_root).string();
        }
    }

    feature_pipeline_configs_.reserve(feature_ids.size());
    for (const std::string& feature_id : feature_ids) {
        const std::string init_config =
            replace_id_template(init_template, feature_id, "module init_config");
        const std::string forcing_pattern =
            replace_id_template(forcing_pattern_template, feature_id,
                                "global forcing file_pattern");

        const std::string init_config_path =
            require_existing_file(resolve_path(base_directory, init_config),
                                  "module init_config");
        const std::string forcing_file =
            match_forcing_file(forcing_directory, forcing_pattern, feature_id);

        BmiModuleConfig task_module_config = module_config;
        task_module_config.config_file_ = init_config_path;

        feature_pipeline_configs_.emplace_back(
            std::vector<BmiModuleConfig>{std::move(task_module_config)}, forcing_file,
            feature_id);
    }
}

// Build one MIAOW task per partitioned realization feature
std::vector<Task> build_bmi_realization_tasks(const PartitionedBmiRealization& realization,
                                              SimulationUnitId simulation_unit_id) {
    // Build one task for each feature in realization order
    std::vector<Task> tasks;
    tasks.reserve(realization.feature_pipeline_configs_.size());
    for (const BmiPipelineConfig& pipeline_config :
         realization.feature_pipeline_configs_) {
        const std::string feature_id = io::trim(pipeline_config.feature_id_);
        if (feature_id.empty()) {
            throw_realization_config_error(
                "pipeline config feature_id must be a non-empty string");
        }

        // Feature metadata provides the hydrofabric join key for map outputs
        Task task(simulation_unit_id, bmi_pipeline_config_to_value(pipeline_config),
                  TaskMetadata{{"feature_id", feature_id},
                               {"feature_type", "catchment"},
                               {"hydrofabric_layer", "divides"},
                               {"hydrofabric_id_field", "id"}});
        tasks.push_back(task);
    }

    return tasks;
}

} // namespace miaow
