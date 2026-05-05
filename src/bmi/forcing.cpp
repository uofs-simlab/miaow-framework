#include "bmi/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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
// Target pairs are BMI input variable name -> CSV header name
using ForcingTargets = std::vector<std::pair<std::string, std::string>>;

struct MappedForcingColumn {
    // BMI input variable supplied by this CSV column
    std::string bmi_input_name_;
    // CSV header selected by the mapping for the BMI input
    std::string csv_header_;
    // Zero-based CSV column index resolved from the header row
    std::size_t csv_column_index_{0};
};

struct ForcingRow {
    // Parsed timestamp from the time column
    std::int64_t timestamp_seconds_{0};
    // Physical one-based line number in the source CSV
    std::size_t row_number_{0};
    // Current row values in the same order as ForcingStreamState::mapped_columns_
    std::vector<double> values_;
};

// Header metadata plus the current forcing row; no full time series is stored
struct ForcingStreamState {
    std::ifstream csv_stream_;
    std::string time_column_name_;
    std::size_t csv_time_column_index_{0};
    std::vector<MappedForcingColumn> mapped_columns_;
    ForcingRow current_row_;
    std::size_t last_csv_row_number_{1};
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
    int expected_n_bytes = 0;
    if (var_type == "double") {
        expected_n_bytes = static_cast<int>(sizeof(double));
    } else if (var_type == "float") {
        expected_n_bytes = static_cast<int>(sizeof(float));
    } else {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + var_name +
            "' must have BMI type 'double' or 'float' but has '" +
            var_type + "'");
    }

    const int n_bytes = model.GetVarNbytes(var_name);
    if (n_bytes != expected_n_bytes) {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + var_name +
            "' must be scalar (" + std::to_string(expected_n_bytes) +
            " bytes) but has " + std::to_string(n_bytes) + " byte(s)");
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

double get_model_time_step_seconds(::bmi::Bmi& model) {
    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error(
            "BMI forcing error: invalid BMI time step '" +
            std::to_string(model_time_step) + "'");
    }

    return model_time_step * time_unit_scale_seconds(model.GetTimeUnits());
}

void validate_time_step_delta(double actual_seconds,
                              double expected_seconds,
                              std::size_t row_number) {
    if (actual_seconds <= 0.0) {
        throw std::runtime_error(
            "Forcing data error: forcing timestamps must increase strictly at row " +
            std::to_string(row_number));
    }

    const double absolute_difference =
        std::abs(actual_seconds - expected_seconds);
    const double tolerance = std::max(
        1e-12,
        std::numeric_limits<double>::epsilon() *
            std::max(std::abs(actual_seconds), std::abs(expected_seconds)) *
            16.0);
    if (absolute_difference > tolerance) {
        std::ostringstream error;
        error.precision(std::numeric_limits<double>::max_digits10);
        error << "Forcing alignment error: forcing timestep "
              << actual_seconds
              << " s must match BMI time step " << expected_seconds
              << " s at row " << row_number;
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

bool read_next_row(std::ifstream& csv_stream,
                   const std::string& time_column_name,
                   std::size_t csv_time_column_index,
                   const std::vector<MappedForcingColumn>& mapped_columns,
                   std::size_t& last_csv_row_number,
                   ForcingRow& row) {
    // Read until the next non-empty row so blank lines do not count as timesteps
    std::string row_line;
    while (std::getline(csv_stream, row_line)) {
        ++last_csv_row_number;
        if (io::trim(row_line).empty()) {
            continue;
        }

        const std::vector<std::string> cells = io::split_csv_line(row_line);
        if (csv_time_column_index >= cells.size()) {
            throw std::runtime_error(
                "Forcing data error: row " + std::to_string(last_csv_row_number) +
                " is missing column '" + time_column_name + "'");
        }

        row.timestamp_seconds_ = parse_timestamp_seconds(
            cells.at(csv_time_column_index),
            time_column_name,
            last_csv_row_number);
        row.row_number_ = last_csv_row_number;
        // Preserve the mapped-column order so BMI names index directly into values_
        row.values_.clear();
        row.values_.reserve(mapped_columns.size());
        for (const MappedForcingColumn& column : mapped_columns) {
            if (column.csv_column_index_ >= cells.size()) {
                throw std::runtime_error(
                    "Forcing data error: row " +
                    std::to_string(last_csv_row_number) +
                    " is missing column '" + column.csv_header_ + "'");
            }
            row.values_.push_back(parse_numeric_value(
                cells.at(column.csv_column_index_),
                column.csv_header_,
                last_csv_row_number));
        }
        return true;
    }

    return false;
}

ForcingStreamState open_forcing_stream(
    const std::filesystem::path& forcing_path,
    const ForcingTargets& targets,
    double time_step_seconds) {
    ForcingStreamState state;
    state.time_step_seconds_ = time_step_seconds;
    state.csv_stream_.open(forcing_path);
    if (!state.csv_stream_.is_open()) {
        throw std::runtime_error(
            "Forcing data error: unable to open CSV file '" +
            forcing_path.string() + "'");
    }

    std::string header_line;
    if (!std::getline(state.csv_stream_, header_line)) {
        throw std::runtime_error(
            "Forcing data error: CSV file '" + forcing_path.string() +
            "' is missing a header row");
    }

    // Startup validation resolves all mapped columns from the CSV header
    const std::vector<std::string> headers = io::split_csv_line(header_line);
    const auto time_it = std::find(headers.begin(), headers.end(), "time");
    if (time_it != headers.end()) {
        state.csv_time_column_index_ = static_cast<std::size_t>(
            std::distance(headers.begin(), time_it));
        state.time_column_name_ = "time";
    } else {
        const auto upper_time_it = std::find(headers.begin(), headers.end(), "Time");
        if (upper_time_it == headers.end()) {
            throw std::runtime_error(
                "Forcing data error: CSV file '" + forcing_path.string() +
                "' must define a 'time' or 'Time' column");
        }
        state.csv_time_column_index_ = static_cast<std::size_t>(
            std::distance(headers.begin(), upper_time_it));
        state.time_column_name_ = "Time";
    }

    // Store the BMI output name, source header, and CSV index together
    state.mapped_columns_.reserve(targets.size());
    for (const auto& [input_name, csv_header] : targets) {
        const auto header_it = std::find(headers.begin(),
                                         headers.end(),
                                         csv_header);
        if (header_it == headers.end()) {
            throw std::runtime_error(
                "Forcing data error: column '" + csv_header +
                "' not found in CSV header");
        }
        state.mapped_columns_.push_back(
            MappedForcingColumn{
                input_name,
                csv_header,
                static_cast<std::size_t>(
                    std::distance(headers.begin(), header_it))});
    }

    // Read the first data row now so GetValue can serve values before Update
    if (!read_next_row(state.csv_stream_,
                       state.time_column_name_,
                       state.csv_time_column_index_,
                       state.mapped_columns_,
                       state.last_csv_row_number_,
                       state.current_row_)) {
        throw std::runtime_error(
            "Forcing data error: CSV file '" + forcing_path.string() +
            "' must contain at least one data row");
    }

    return state;
}

// Minimal BMI adapter that streams mapped forcing rows

[[noreturn]] void throw_unsupported(const std::string& operation) {
    throw std::runtime_error("ForcingBmi: " + operation + " not supported");
}

class ForcingBmi : public ::bmi::Bmi {
public:
    explicit ForcingBmi(ForcingStreamState state)
        : csv_stream_(std::move(state.csv_stream_)),
          time_column_name_(std::move(state.time_column_name_)),
          csv_time_column_index_(state.csv_time_column_index_),
          mapped_columns_(std::move(state.mapped_columns_)),
          current_row_(std::move(state.current_row_)),
          last_csv_row_number_(state.last_csv_row_number_),
          time_step_seconds_(state.time_step_seconds_) {
    }

    void Initialize(std::string /*config_file*/) override {
        throw_unsupported("Initialize");
    }

    void Update() override {
        if (exhausted_) {
            ++step_;
            current_time_seconds_ += time_step_seconds_;
            return;
        }

        // No lookahead: advancing the forcing BMI consumes exactly one CSV row
        ForcingRow next_row;
        if (!read_next_row(csv_stream_,
                           time_column_name_,
                           csv_time_column_index_,
                           mapped_columns_,
                           last_csv_row_number_,
                           next_row)) {
            exhausted_ = true;
            ++step_;
            current_time_seconds_ += time_step_seconds_;
            return;
        }

        // Timestamp spacing is validated as rows stream in
        const double delta_seconds = static_cast<double>(
            next_row.timestamp_seconds_ - current_row_.timestamp_seconds_);
        validate_time_step_delta(delta_seconds,
                                 time_step_seconds_,
                                 next_row.row_number_);
        current_row_ = std::move(next_row);
        ++step_;
        current_time_seconds_ += time_step_seconds_;
    }

    void UpdateUntil(double /*time*/) override { throw_unsupported("UpdateUntil"); }

    void Finalize() override {
        mapped_columns_.clear();
        current_row_.values_.clear();
        if (csv_stream_.is_open()) {
            csv_stream_.close();
        }
        step_ = 0;
        current_time_seconds_ = 0.0;
        exhausted_ = true;
    }

    std::string GetComponentName() override { return "ForcingBmi"; }
    int GetInputItemCount() override { return 0; }
    int GetOutputItemCount() override {
        return static_cast<int>(mapped_columns_.size());
    }
    std::vector<std::string> GetInputVarNames() override { return {}; }
    std::vector<std::string> GetOutputVarNames() override {
        std::vector<std::string> output_names;
        output_names.reserve(mapped_columns_.size());
        for (const MappedForcingColumn& column : mapped_columns_) {
            output_names.push_back(column.bmi_input_name_);
        }
        return output_names;
    }

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
        // The end time is unknown without scanning the remaining CSV rows
        return std::numeric_limits<double>::infinity();
    }
    std::string GetTimeUnits() override { return "s"; }
    double GetTimeStep() override { return time_step_seconds_; }

    void GetValue(std::string name, void* dest) override {
        const std::size_t output_index = find_output_index(name);
        if (exhausted_) {
            throw std::runtime_error(
                "Forcing alignment error: forcing series exhausted at step " +
                std::to_string(step_));
        }
        *static_cast<double*>(dest) = current_row_.values_.at(output_index);
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
        for (std::size_t i = 0; i < mapped_columns_.size(); ++i) {
            if (mapped_columns_[i].bmi_input_name_ == name) {
                return i;
            }
        }
        throw std::runtime_error("ForcingBmi: unknown variable '" + name + "'");
    }

    void check_grid_id(int grid) const {
        if (grid != 0) {
            throw std::runtime_error(
                "ForcingBmi: unknown grid " + std::to_string(grid));
        }
    }

    // Streaming CSV source positioned after the current forcing row
    std::ifstream csv_stream_;
    // Time column metadata from the CSV header
    std::string time_column_name_;
    std::size_t csv_time_column_index_{0};
    // Mapped forcing columns exposed as BMI outputs in this order
    std::vector<MappedForcingColumn> mapped_columns_;
    // Current row exposed through GetValue until Update reads the next row
    ForcingRow current_row_;
    // Last physical CSV row number read from the stream
    std::size_t last_csv_row_number_{1};
    // Current timestep index returned by GetValue
    std::size_t step_{0};
    // Current forcing time exposed through the BMI clock methods
    double current_time_seconds_{0.0};
    // Expected forcing timestep from the model BMI clock
    double time_step_seconds_{0.0};
    // True after Update reaches the end of the forcing CSV
    bool exhausted_{false};
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

    for (const ForcingTargets::value_type& forcing_target : forcing_targets) {
        // Check mapped BMI inputs before loading CSV data for them
        validate_driver_input(model, forcing_target.first);
    }

    const double time_step_seconds = get_model_time_step_seconds(model);
    ForcingStreamState forcing_state =
        open_forcing_stream(forcing_path,
                            forcing_targets,
                            time_step_seconds);
    return std::make_unique<ForcingBmi>(std::move(forcing_state));
}

} // namespace miaow::bmi
