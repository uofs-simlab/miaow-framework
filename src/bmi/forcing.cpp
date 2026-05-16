#include "bmi/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "io/internal.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

namespace {

// Forcing path and target clock helpers

std::filesystem::path resolve_forcing_path(const std::string& raw_path) {
    const std::string trimmed_path = io::trim(raw_path);
    if (trimmed_path.empty()) {
        throw std::runtime_error(
            "Forcing config error: forcing file path must be a non-empty string");
    }

    std::filesystem::path forcing_path = std::filesystem::path(trimmed_path);
    if (forcing_path.is_relative()) {
        forcing_path = std::filesystem::current_path() / forcing_path;
    }
    forcing_path = std::filesystem::absolute(forcing_path).lexically_normal();

    if (!std::filesystem::exists(forcing_path)) {
        throw std::runtime_error("Forcing config error: forcing_file path '" +
                                 forcing_path.string() + "' does not exist");
    }
    if (!std::filesystem::is_regular_file(forcing_path)) {
        throw std::runtime_error("Forcing config error: forcing_file path '" +
                                 forcing_path.string() + "' is not a regular file");
    }

    return forcing_path;
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

double time_unit_scale_seconds(const std::string& raw_units) {
    // Normalize common BMI unit strings to seconds for timestep comparison
    const std::string units = to_lower_ascii(io::trim(raw_units));
    if (units == "s" || units == "sec" || units == "second" || units == "seconds") {
        return 1.0;
    }
    if (units == "h" || units == "hr" || units == "hour" || units == "hours") {
        return 3600.0;
    }
    if (units == "d" || units == "day" || units == "days") {
        return 86400.0;
    }
    throw std::runtime_error("BMI forcing error: unsupported BMI time units '" + raw_units +
                             "'");
}

double get_model_time_step_seconds(::bmi::Bmi& model) {
    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error("BMI forcing error: invalid BMI time step '" +
                                 std::to_string(model_time_step) + "'");
    }

    return model_time_step * time_unit_scale_seconds(model.GetTimeUnits());
}

void validate_time_step_delta(double actual_seconds, double expected_seconds,
                              std::size_t row_number) {
    if (actual_seconds <= 0.0) {
        throw std::runtime_error(
            "Forcing data error: forcing timestamps must increase strictly at row " +
            std::to_string(row_number));
    }

    const double absolute_difference = std::abs(actual_seconds - expected_seconds);
    const double tolerance =
        std::max(1e-12, std::numeric_limits<double>::epsilon() *
                            std::max(std::abs(actual_seconds), std::abs(expected_seconds)) *
                            16.0);
    if (absolute_difference > tolerance) {
        std::ostringstream error;
        error.precision(std::numeric_limits<double>::max_digits10);
        error << "Forcing alignment error: forcing timestep " << actual_seconds
              << " s must match BMI time step " << expected_seconds << " s at row "
              << row_number;
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
        throw std::runtime_error("Forcing data error: invalid timestamp in column '" +
                                 time_column_name + "' at row " +
                                 std::to_string(row_number));
    }
}

double parse_numeric_value(const std::string& raw_value, const std::string& column_name,
                           std::size_t row_number) {
    std::size_t parse_pos = 0;
    try {
        const double value = std::stod(raw_value, &parse_pos);
        if (parse_pos != raw_value.size() || !std::isfinite(value)) {
            throw std::runtime_error("invalid");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("Forcing data error: non-numeric value in column '" +
                                 column_name + "' at row " + std::to_string(row_number));
    }
}

[[noreturn]] void throw_unsupported(const std::string& operation) {
    throw std::runtime_error("ForcingBmi: " + operation + " not supported");
}

} // namespace

ForcingBmi::ForcingBmi(::bmi::Bmi& target_model) : target_model_(target_model) {}

void ForcingBmi::open_stream(const std::filesystem::path& forcing_path) {
    csv_stream_.open(forcing_path);
    if (!csv_stream_.is_open()) {
        throw std::runtime_error("Forcing data error: unable to open CSV file '" +
                                 forcing_path.string() + "'");
    }

    std::string header_line;
    if (!std::getline(csv_stream_, header_line)) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' is missing a header row");
    }

    // Startup validation resolves time and forcing columns from the CSV header
    const std::vector<std::string> headers = io::split_csv_line(header_line);
    const std::vector<std::string>::const_iterator time_it =
        std::find_if(headers.begin(), headers.end(), [](const std::string& header) {
            return header == "time" || header == "Time";
        });
    if (time_it == headers.end()) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must define a 'time' or 'Time' column");
    }
    csv_time_column_index_ =
        static_cast<std::size_t>(std::distance(headers.begin(), time_it));
    time_column_name_ = *time_it;

    // Store every non-time CSV data column as a forcing BMI output
    forcing_columns_.reserve(headers.size() - 1);
    for (std::size_t column_index = 0; column_index < headers.size(); ++column_index) {
        if (column_index == csv_time_column_index_) {
            continue;
        }

        const std::string csv_header = io::trim(headers.at(column_index));
        if (csv_header.empty()) {
            throw std::runtime_error("Forcing data error: CSV file '" +
                                     forcing_path.string() +
                                     "' contains an empty data column header");
        }
        forcing_columns_.push_back(ForcingColumn{csv_header, column_index});
    }

    if (forcing_columns_.empty()) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must define at least one data column");
    }

    // Read the first data row now so GetValue can serve values before Update
    if (!read_next_row(current_row_)) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must contain at least one data row");
    }
}

bool ForcingBmi::read_next_row(ForcingRow& row) {
    // Read until the next non-empty row so blank lines do not count as timesteps
    std::string row_line;
    while (std::getline(csv_stream_, row_line)) {
        ++last_csv_row_number_;
        if (io::trim(row_line).empty()) {
            continue;
        }

        const std::vector<std::string> cells = io::split_csv_line(row_line);
        if (csv_time_column_index_ >= cells.size()) {
            throw std::runtime_error("Forcing data error: row " +
                                     std::to_string(last_csv_row_number_) +
                                     " is missing column '" + time_column_name_ + "'");
        }

        row.timestamp_seconds_ =
            parse_timestamp_seconds(cells.at(csv_time_column_index_), time_column_name_,
                                    last_csv_row_number_);
        row.row_number_ = last_csv_row_number_;

        // Preserve output column order so BMI names index directly into values_
        row.values_.clear();
        row.values_.reserve(forcing_columns_.size());
        for (const ForcingColumn& column : forcing_columns_) {
            if (column.csv_column_index_ >= cells.size()) {
                throw std::runtime_error("Forcing data error: row " +
                                         std::to_string(last_csv_row_number_) +
                                         " is missing column '" + column.csv_header_ + "'");
            }
            row.values_.push_back(parse_numeric_value(cells.at(column.csv_column_index_),
                                                      column.csv_header_,
                                                      last_csv_row_number_));
        }
        return true;
    }

    return false;
}

void ForcingBmi::Initialize(std::string config_file) {
    Finalize();

    // Empty config means this forcing module is present but inactive
    if (io::trim(config_file).empty()) {
        return;
    }

    // The config string for this internal module is the forcing CSV path
    const std::filesystem::path forcing_path = resolve_forcing_path(config_file);

    // The receiving model clock defines the expected forcing timestep
    time_step_seconds_ = get_model_time_step_seconds(target_model_);
    open_stream(forcing_path);

    // The first CSV data row is already loaded by open_stream
    step_ = 0;
    current_time_seconds_ = 0.0;
    exhausted_ = false;
    active_ = true;
}

void ForcingBmi::Update() {
    if (!active_) {
        return;
    }

    if (exhausted_) {
        ++step_;
        current_time_seconds_ += time_step_seconds_;
        return;
    }

    // No lookahead: advancing the forcing BMI consumes exactly one CSV row
    ForcingRow next_row;
    if (!read_next_row(next_row)) {
        exhausted_ = true;
        ++step_;
        current_time_seconds_ += time_step_seconds_;
        return;
    }

    // Timestamp spacing is validated as rows stream in
    const double delta_seconds =
        static_cast<double>(next_row.timestamp_seconds_ - current_row_.timestamp_seconds_);
    validate_time_step_delta(delta_seconds, time_step_seconds_, next_row.row_number_);
    current_row_ = std::move(next_row);
    ++step_;
    current_time_seconds_ += time_step_seconds_;
}

void ForcingBmi::UpdateUntil(double /*time*/) {
    throw_unsupported("UpdateUntil");
}

void ForcingBmi::Finalize() {
    // Reset all stream-derived state so Initialize can safely be called again
    forcing_columns_.clear();
    current_row_.values_.clear();
    if (csv_stream_.is_open()) {
        csv_stream_.close();
    }
    time_column_name_.clear();
    csv_time_column_index_ = 0;
    last_csv_row_number_ = 1;
    step_ = 0;
    current_time_seconds_ = 0.0;
    time_step_seconds_ = 0.0;
    exhausted_ = true;
    active_ = false;
}

std::string ForcingBmi::GetComponentName() {
    return "ForcingBmi";
}

int ForcingBmi::GetInputItemCount() {
    return 0;
}

int ForcingBmi::GetOutputItemCount() {
    return static_cast<int>(forcing_columns_.size());
}

std::vector<std::string> ForcingBmi::GetInputVarNames() {
    return {};
}

std::vector<std::string> ForcingBmi::GetOutputVarNames() {
    std::vector<std::string> output_names;
    output_names.reserve(forcing_columns_.size());
    for (const ForcingColumn& column : forcing_columns_) {
        output_names.push_back(column.csv_header_);
    }
    return output_names;
}

int ForcingBmi::GetVarGrid(std::string name) {
    find_output_index(name);
    return 0;
}

std::string ForcingBmi::GetVarType(std::string name) {
    find_output_index(name);
    return "double";
}

std::string ForcingBmi::GetVarUnits(std::string name) {
    find_output_index(name);
    return "";
}

int ForcingBmi::GetVarItemsize(std::string name) {
    find_output_index(name);
    return static_cast<int>(sizeof(double));
}

int ForcingBmi::GetVarNbytes(std::string name) {
    find_output_index(name);
    return static_cast<int>(sizeof(double));
}

std::string ForcingBmi::GetVarLocation(std::string name) {
    find_output_index(name);
    return "node";
}

double ForcingBmi::GetCurrentTime() {
    return current_time_seconds_;
}

double ForcingBmi::GetStartTime() {
    return 0.0;
}

double ForcingBmi::GetEndTime() {
    // The end time is unknown without scanning the remaining CSV rows
    return std::numeric_limits<double>::infinity();
}

std::string ForcingBmi::GetTimeUnits() {
    return "s";
}

double ForcingBmi::GetTimeStep() {
    return time_step_seconds_;
}

void ForcingBmi::GetValue(std::string name, void* dest) {
    const std::size_t output_index = find_output_index(name);
    if (exhausted_) {
        throw std::runtime_error(
            "Forcing alignment error: forcing series exhausted at step " +
            std::to_string(step_));
    }
    *static_cast<double*>(dest) = current_row_.values_.at(output_index);
}

void* ForcingBmi::GetValuePtr(std::string /*name*/) {
    throw_unsupported("GetValuePtr");
}

void ForcingBmi::GetValueAtIndices(std::string /*name*/, void* /*dest*/, int* /*inds*/,
                                   int /*count*/) {
    throw_unsupported("GetValueAtIndices");
}

void ForcingBmi::SetValue(std::string /*name*/, void* /*src*/) {
    throw_unsupported("SetValue");
}

void ForcingBmi::SetValueAtIndices(std::string /*name*/, int* /*inds*/, int /*count*/,
                                   void* /*src*/) {
    throw_unsupported("SetValueAtIndices");
}

int ForcingBmi::GetGridRank(const int grid) {
    check_grid_id(grid);
    return 0;
}

int ForcingBmi::GetGridSize(const int grid) {
    check_grid_id(grid);
    return 1;
}

std::string ForcingBmi::GetGridType(const int grid) {
    check_grid_id(grid);
    return "scalar";
}

void ForcingBmi::GetGridShape(const int /*grid*/, int* /*shape*/) {
    throw_unsupported("GetGridShape");
}

void ForcingBmi::GetGridSpacing(const int /*grid*/, double* /*spacing*/) {
    throw_unsupported("GetGridSpacing");
}

void ForcingBmi::GetGridOrigin(const int /*grid*/, double* /*origin*/) {
    throw_unsupported("GetGridOrigin");
}

void ForcingBmi::GetGridX(const int /*grid*/, double* /*x*/) {
    throw_unsupported("GetGridX");
}

void ForcingBmi::GetGridY(const int /*grid*/, double* /*y*/) {
    throw_unsupported("GetGridY");
}

void ForcingBmi::GetGridZ(const int /*grid*/, double* /*z*/) {
    throw_unsupported("GetGridZ");
}

int ForcingBmi::GetGridNodeCount(const int /*grid*/) {
    throw_unsupported("GetGridNodeCount");
}

int ForcingBmi::GetGridEdgeCount(const int /*grid*/) {
    throw_unsupported("GetGridEdgeCount");
}

int ForcingBmi::GetGridFaceCount(const int /*grid*/) {
    throw_unsupported("GetGridFaceCount");
}

void ForcingBmi::GetGridEdgeNodes(const int /*grid*/, int* /*edge_nodes*/) {
    throw_unsupported("GetGridEdgeNodes");
}

void ForcingBmi::GetGridFaceEdges(const int /*grid*/, int* /*face_edges*/) {
    throw_unsupported("GetGridFaceEdges");
}

void ForcingBmi::GetGridFaceNodes(const int /*grid*/, int* /*face_nodes*/) {
    throw_unsupported("GetGridFaceNodes");
}

void ForcingBmi::GetGridNodesPerFace(const int /*grid*/, int* /*nodes_per_face*/) {
    throw_unsupported("GetGridNodesPerFace");
}

std::size_t ForcingBmi::find_output_index(const std::string& name) const {
    for (std::size_t i = 0; i < forcing_columns_.size(); ++i) {
        if (forcing_columns_[i].csv_header_ == name) {
            return i;
        }
    }
    throw std::runtime_error("ForcingBmi: unknown variable '" + name + "'");
}

void ForcingBmi::check_grid_id(int grid) const {
    if (grid != 0) {
        throw std::runtime_error("ForcingBmi: unknown grid " + std::to_string(grid));
    }
}

} // namespace miaow::bmi
