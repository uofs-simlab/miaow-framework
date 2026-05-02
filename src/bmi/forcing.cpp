#include "bmi/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "io/internal.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

namespace {

using ForcingDictionary = caf::config_value::dictionary;
using ForcingTargets = std::vector<std::pair<std::string, std::string>>;

struct LoadedForcingData {
    std::vector<std::string> output_names_;
    std::vector<std::vector<double>> output_values_;
    std::size_t row_count_{0};
    double time_step_seconds_{0.0};
};

// Forcing config and target selection helpers

std::string require_non_empty_string(const ForcingDictionary& forcing_config,
                                     const std::string& key) {
    const auto it = forcing_config.find(key);
    if (it == forcing_config.end()) {
        throw std::runtime_error(
            "Forcing config error: missing key '" + key + "'");
    }

    const std::string* value = caf::get_if<std::string>(&it->second);
    if (!value) {
        throw std::runtime_error(
            "Forcing config error: key '" + key + "' must be a string");
    }

    const std::string parsed = io::trim(*value);
    if (parsed.empty()) {
        throw std::runtime_error(
            "Forcing config error: key '" + key + "' must be a non-empty string");
    }
    return parsed;
}

std::filesystem::path resolve_forcing_path(const std::string& raw_path) {
    std::filesystem::path forcing_path = std::filesystem::path(raw_path);
    if (forcing_path.is_relative()) {
        forcing_path = std::filesystem::current_path() / forcing_path;
    }
    forcing_path = std::filesystem::absolute(forcing_path).lexically_normal();

    if (!std::filesystem::exists(forcing_path)) {
        throw std::runtime_error(
            "Forcing config error: forcing_file path '" +
            forcing_path.string() + "' does not exist");
    }
    if (!std::filesystem::is_regular_file(forcing_path)) {
        throw std::runtime_error(
            "Forcing config error: forcing_file path '" +
            forcing_path.string() + "' is not a regular file");
    }

    return forcing_path;
}

ForcingTargets build_explicit_targets(const ForcingDictionary& forcing_config) {
    // Explicit mappings override identity mode and narrow the forced inputs
    const auto it = forcing_config.find("variables_names_map");
    if (it == forcing_config.end()) {
        return {};
    }

    const ForcingDictionary* variables_names_map =
        caf::get_if<ForcingDictionary>(&it->second);
    if (!variables_names_map) {
        throw std::runtime_error(
            "Forcing config error: key 'variables_names_map' must be a "
            "dictionary");
    }
    if (variables_names_map->empty()) {
        throw std::runtime_error(
            "Forcing config error: key 'variables_names_map' must not be empty");
    }

    ForcingTargets targets;
    targets.reserve(variables_names_map->size());
    for (const auto& [raw_input_name, header_value] : *variables_names_map) {
        const std::string input_name = io::trim(raw_input_name);
        if (input_name.empty()) {
            throw std::runtime_error(
                "Forcing config error: key 'variables_names_map' contains an "
                "empty BMI input variable name");
        }

        const std::string* csv_header = caf::get_if<std::string>(&header_value);
        if (!csv_header) {
            throw std::runtime_error(
                "Forcing config error: key 'variables_names_map' entry '" +
                input_name + "' must be a string");
        }

        const std::string trimmed_header = io::trim(*csv_header);
        if (trimmed_header.empty()) {
            throw std::runtime_error(
                "Forcing config error: key 'variables_names_map' entry '" +
                input_name + "' must be a non-empty string");
        }

        targets.emplace_back(input_name, trimmed_header);
    }
    return targets;
}

ForcingTargets build_identity_targets(::bmi::Bmi& model) {
    // Identity mode forces every BMI input from a same-named CSV header
    const std::vector<std::string> input_vars = model.GetInputVarNames();
    if (input_vars.empty()) {
        throw std::runtime_error(
            "BMI forcing error: identity variables_names_map requires at least "
            "one BMI input variable");
    }

    ForcingTargets targets;
    targets.reserve(input_vars.size());
    for (const std::string& input_var : input_vars) {
        const std::string trimmed_input = io::trim(input_var);
        if (trimmed_input.empty()) {
            throw std::runtime_error(
                "BMI forcing error: identity variables_names_map encountered "
                "an empty BMI input variable name");
        }
        targets.emplace_back(trimmed_input, trimmed_input);
    }
    return targets;
}

// BMI-side validation helpers

void validate_driver_input(::bmi::Bmi& model, const std::string& var_name) {
    const std::vector<std::string> input_vars = model.GetInputVarNames();
    if (std::find(input_vars.begin(), input_vars.end(), var_name) ==
        input_vars.end()) {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + var_name + "' not found");
    }

    const std::string var_type = model.GetVarType(var_name);
    if (var_type != "double") {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + var_name +
            "' must have BMI type 'double' but has '" + var_type + "'");
    }

    const int n_bytes = model.GetVarNbytes(var_name);
    if (n_bytes != static_cast<int>(sizeof(double))) {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + var_name +
            "' must be scalar (8 bytes) but has " + std::to_string(n_bytes) +
            " byte(s)");
    }
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

double time_unit_scale_seconds(const std::string& raw_units) {
    // Normalize common BMI unit strings to seconds for timestep comparison
    const std::string units = to_lower_ascii(io::trim(raw_units));
    if (units == "s" || units == "sec" || units == "second" ||
        units == "seconds") {
        return 1.0;
    }
    if (units == "h" || units == "hr" || units == "hour" ||
        units == "hours") {
        return 3600.0;
    }
    if (units == "d" || units == "day" || units == "days") {
        return 86400.0;
    }
    throw std::runtime_error(
        "BMI forcing error: unsupported BMI time units '" + raw_units + "'");
}

void validate_time_alignment(double forcing_time_step_seconds,
                             ::bmi::Bmi& model) {
    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error(
            "BMI forcing error: invalid BMI time step '" +
            std::to_string(model_time_step) + "'");
    }

    const double model_time_step_seconds =
        model_time_step * time_unit_scale_seconds(model.GetTimeUnits());
    const double absolute_difference =
        std::abs(forcing_time_step_seconds - model_time_step_seconds);
    const double tolerance = std::max(
        1e-12,
        std::numeric_limits<double>::epsilon() *
            std::max(std::abs(forcing_time_step_seconds),
                     std::abs(model_time_step_seconds)) *
            16.0);
    if (absolute_difference > tolerance) {
        std::ostringstream error;
        error.precision(std::numeric_limits<double>::max_digits10);
        error << "BMI forcing error: forcing timestep "
              << forcing_time_step_seconds
              << " s must match BMI time step " << model_time_step << " "
              << model.GetTimeUnits();
        throw std::runtime_error(error.str());
    }
}

// CSV parsing and loading helpers

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp,
                                     const std::string& time_column_name,
                                     std::size_t row_number) {
    try {
        return io::parse_timestamp_seconds(raw_timestamp);
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Forcing data error: invalid timestamp in column '" +
            time_column_name + "' at row " + std::to_string(row_number));
    }
}

double parse_numeric_value(const std::string& raw_value,
                           const std::string& column_name,
                           std::size_t row_number) {
    std::size_t parse_pos = 0;
    try {
        const double value = std::stod(raw_value, &parse_pos);
        if (parse_pos != raw_value.size() || !std::isfinite(value)) {
            throw std::runtime_error("invalid");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Forcing data error: non-numeric value in column '" +
            column_name + "' at row " + std::to_string(row_number));
    }
}

LoadedForcingData load_forcing_data(const std::filesystem::path& forcing_path,
                                    const ForcingTargets& targets) {
    std::ifstream csv_stream(forcing_path);
    if (!csv_stream.is_open()) {
        throw std::runtime_error(
            "Forcing data error: unable to open CSV file '" +
            forcing_path.string() + "'");
    }

    std::string header_line;
    if (!std::getline(csv_stream, header_line)) {
        throw std::runtime_error(
            "Forcing data error: CSV file '" + forcing_path.string() +
            "' is missing a header row");
    }

    const std::vector<std::string> headers = io::split_csv_line(header_line);
    std::size_t time_index = headers.size();
    std::string time_column_name;
    const auto time_it = std::find(headers.begin(), headers.end(), "time");
    if (time_it != headers.end()) {
        time_index = static_cast<std::size_t>(
            std::distance(headers.begin(), time_it));
        time_column_name = "time";
    } else {
        const auto upper_time_it = std::find(headers.begin(), headers.end(), "Time");
        if (upper_time_it != headers.end()) {
            time_index = static_cast<std::size_t>(
                std::distance(headers.begin(), upper_time_it));
            time_column_name = "Time";
        }
    }
    if (time_index == headers.size()) {
        throw std::runtime_error(
            "Forcing data error: CSV file '" + forcing_path.string() +
            "' must define a 'time' or 'Time' column");
    }

    std::map<std::string, std::size_t> required_column_indexes;
    for (const auto& [input_name, csv_header] : targets) {
        static_cast<void>(input_name);
        // Reused CSV headers are loaded once and fanned out to multiple inputs
        const auto it = required_column_indexes.find(csv_header);
        if (it != required_column_indexes.end()) {
            continue;
        }
        const auto header_it = std::find(headers.begin(), headers.end(), csv_header);
        if (header_it == headers.end()) {
            throw std::runtime_error(
                "Forcing data error: column '" + csv_header +
                "' not found in CSV header");
        }
        required_column_indexes.emplace(
            csv_header,
            static_cast<std::size_t>(std::distance(headers.begin(), header_it)));
    }

    std::map<std::string, std::vector<double>> column_values;
    for (const auto& [csv_header, unused_index] : required_column_indexes) {
        static_cast<void>(unused_index);
        column_values.emplace(csv_header, std::vector<double>{});
    }

    std::vector<std::int64_t> timestamps;
    std::string row_line;
    std::size_t row_number = 1;
    while (std::getline(csv_stream, row_line)) {
        ++row_number;
        if (io::trim(row_line).empty()) {
            continue;
        }

        const std::vector<std::string> cells = io::split_csv_line(row_line);
        if (time_index >= cells.size()) {
            throw std::runtime_error(
                "Forcing data error: row " + std::to_string(row_number) +
                " is missing column '" + time_column_name + "'");
        }

        timestamps.push_back(parse_timestamp_seconds(cells.at(time_index),
                                                     time_column_name,
                                                     row_number));
        for (const auto& [csv_header, column_index] : required_column_indexes) {
            if (column_index >= cells.size()) {
                throw std::runtime_error(
                    "Forcing data error: row " + std::to_string(row_number) +
                    " is missing column '" + csv_header + "'");
            }

            column_values.at(csv_header).push_back(
                parse_numeric_value(cells.at(column_index), csv_header, row_number));
        }
    }

    if (timestamps.size() < 2) {
        throw std::runtime_error(
            "Forcing data error: CSV file '" + forcing_path.string() +
            "' must contain at least two data rows");
    }

    // The external forcing contract is one fixed timestep for the whole file
    const std::int64_t first_delta = timestamps.at(1) - timestamps.at(0);
    if (first_delta <= 0) {
        throw std::runtime_error(
            "Forcing data error: forcing timestamps must increase strictly");
    }
    for (std::size_t index = 2; index < timestamps.size(); ++index) {
        if (timestamps.at(index) - timestamps.at(index - 1) != first_delta) {
            throw std::runtime_error(
                "Forcing data error: forcing timesteps are not uniform");
        }
    }

    LoadedForcingData data;
    data.row_count_ = timestamps.size();
    data.time_step_seconds_ = static_cast<double>(first_delta);
    data.output_names_.reserve(targets.size());
    data.output_values_.reserve(targets.size());
    for (const auto& [input_name, csv_header] : targets) {
        data.output_names_.push_back(input_name);
        data.output_values_.push_back(column_values.at(csv_header));
    }
    return data;
}

// Minimal BMI adapter that replays loaded forcing rows

[[noreturn]] void throw_unsupported(const std::string& operation) {
    throw std::runtime_error("ForcingBmi: " + operation + " not supported");
}

class ForcingBmi : public ::bmi::Bmi {
public:
    explicit ForcingBmi(LoadedForcingData data)
        : output_names_(std::move(data.output_names_)),
          output_values_(std::move(data.output_values_)),
          row_count_(data.row_count_),
          time_step_seconds_(data.time_step_seconds_) {
    }

    void Initialize(std::string /*config_file*/) override {
        throw_unsupported("Initialize");
    }

    void Update() override {
        ++step_;
        current_time_seconds_ += time_step_seconds_;
    }

    void UpdateUntil(double /*time*/) override { throw_unsupported("UpdateUntil"); }

    void Finalize() override {
        output_names_.clear();
        output_values_.clear();
        step_ = 0;
        current_time_seconds_ = 0.0;
    }

    std::string GetComponentName() override { return "ForcingBmi"; }
    int GetInputItemCount() override { return 0; }
    int GetOutputItemCount() override {
        return static_cast<int>(output_names_.size());
    }
    std::vector<std::string> GetInputVarNames() override { return {}; }
    std::vector<std::string> GetOutputVarNames() override { return output_names_; }

    int GetVarGrid(std::string name) override {
        find_output_index(name);
        return 0;
    }
    std::string GetVarType(std::string name) override {
        find_output_index(name);
        return "double";
    }
    std::string GetVarUnits(std::string name) override {
        find_output_index(name);
        return "";
    }
    int GetVarItemsize(std::string name) override {
        find_output_index(name);
        return static_cast<int>(sizeof(double));
    }
    int GetVarNbytes(std::string name) override {
        find_output_index(name);
        return static_cast<int>(sizeof(double));
    }
    std::string GetVarLocation(std::string name) override {
        find_output_index(name);
        return "node";
    }

    double GetCurrentTime() override { return current_time_seconds_; }
    double GetStartTime() override { return 0.0; }
    double GetEndTime() override {
        return static_cast<double>(row_count_) * time_step_seconds_;
    }
    std::string GetTimeUnits() override { return "s"; }
    double GetTimeStep() override { return time_step_seconds_; }

    void GetValue(std::string name, void* dest) override {
        const std::size_t output_index = find_output_index(name);
        if (step_ >= row_count_) {
            throw std::runtime_error(
                "Forcing alignment error: forcing series exhausted at step " +
                std::to_string(step_));
        }
        *static_cast<double*>(dest) = output_values_.at(output_index).at(step_);
    }

    void* GetValuePtr(std::string /*name*/) override {
        throw_unsupported("GetValuePtr");
    }
    void GetValueAtIndices(std::string /*name*/, void* /*dest*/, int* /*inds*/,
                           int /*count*/) override {
        throw_unsupported("GetValueAtIndices");
    }

    void SetValue(std::string /*name*/, void* /*src*/) override {
        throw_unsupported("SetValue");
    }
    void SetValueAtIndices(std::string /*name*/, int* /*inds*/, int /*count*/,
                           void* /*src*/) override {
        throw_unsupported("SetValueAtIndices");
    }

    int GetGridRank(const int grid) override {
        check_grid_id(grid);
        return 0;
    }
    int GetGridSize(const int grid) override {
        check_grid_id(grid);
        return 1;
    }
    std::string GetGridType(const int grid) override {
        check_grid_id(grid);
        return "scalar";
    }

    void GetGridShape(const int /*grid*/, int* /*shape*/) override {
        throw_unsupported("GetGridShape");
    }
    void GetGridSpacing(const int /*grid*/, double* /*spacing*/) override {
        throw_unsupported("GetGridSpacing");
    }
    void GetGridOrigin(const int /*grid*/, double* /*origin*/) override {
        throw_unsupported("GetGridOrigin");
    }
    void GetGridX(const int /*grid*/, double* /*x*/) override {
        throw_unsupported("GetGridX");
    }
    void GetGridY(const int /*grid*/, double* /*y*/) override {
        throw_unsupported("GetGridY");
    }
    void GetGridZ(const int /*grid*/, double* /*z*/) override {
        throw_unsupported("GetGridZ");
    }
    int GetGridNodeCount(const int /*grid*/) override {
        throw_unsupported("GetGridNodeCount");
    }
    int GetGridEdgeCount(const int /*grid*/) override {
        throw_unsupported("GetGridEdgeCount");
    }
    int GetGridFaceCount(const int /*grid*/) override {
        throw_unsupported("GetGridFaceCount");
    }
    void GetGridEdgeNodes(const int /*grid*/, int* /*edge_nodes*/) override {
        throw_unsupported("GetGridEdgeNodes");
    }
    void GetGridFaceEdges(const int /*grid*/, int* /*face_edges*/) override {
        throw_unsupported("GetGridFaceEdges");
    }
    void GetGridFaceNodes(const int /*grid*/, int* /*face_nodes*/) override {
        throw_unsupported("GetGridFaceNodes");
    }
    void GetGridNodesPerFace(const int /*grid*/,
                             int* /*nodes_per_face*/) override {
        throw_unsupported("GetGridNodesPerFace");
    }

private:
    std::size_t find_output_index(const std::string& name) const {
        const auto it = std::find(output_names_.begin(), output_names_.end(), name);
        if (it == output_names_.end()) {
            throw std::runtime_error(
                "ForcingBmi: unknown variable '" + name + "'");
        }
        return static_cast<std::size_t>(std::distance(output_names_.begin(), it));
    }

    void check_grid_id(int grid) const {
        if (grid != 0) {
            throw std::runtime_error(
                "ForcingBmi: unknown grid " + std::to_string(grid));
        }
    }

    // BMI output names exposed by this forcing driver instance
    std::vector<std::string> output_names_;
    // Column-major forcing values indexed by output name then timestep
    std::vector<std::vector<double>> output_values_;
    // Number of synchronized timesteps available to replay
    std::size_t row_count_{0};
    // Current timestep index returned by GetValue
    std::size_t step_{0};
    // Current forcing time exposed through the BMI clock methods
    double current_time_seconds_{0.0};
    // Uniform forcing timestep inferred from the CSV time column
    double time_step_seconds_{0.0};
};

} // namespace

std::unique_ptr<::bmi::Bmi> make_forcing_bmi(
    const caf::config_value::dictionary& forcing_config,
    ::bmi::Bmi& model) {
    const std::filesystem::path forcing_path =
        resolve_forcing_path(require_non_empty_string(forcing_config,
                                                      "forcing_file"));

    // Prefer explicit mappings and fall back to identity mode only when absent
    ForcingTargets forcing_targets = build_explicit_targets(forcing_config);
    if (forcing_targets.empty()) {
        forcing_targets = build_identity_targets(model);
    }

    for (const auto& [input_name, csv_header] : forcing_targets) {
        static_cast<void>(csv_header);
        validate_driver_input(model, input_name);
    }

    LoadedForcingData forcing_data = load_forcing_data(forcing_path, forcing_targets);
    validate_time_alignment(forcing_data.time_step_seconds_, model);
    return std::make_unique<ForcingBmi>(std::move(forcing_data));
}

} // namespace miaow::bmi
