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

double get_model_time_step_seconds(::bmi::Bmi& model) {
    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error("BMI forcing error: invalid BMI time step '" +
                                 std::to_string(model_time_step) + "'");
    }

    return model_time_step *
           seconds_per_time_unit(model.GetTimeUnits(), "BMI forcing error: ");
}

// CSV parsing and loading helpers

std::int64_t parse_timestamp_seconds(const std::string& raw_timestamp,
                                     std::size_t row_number) {
    try {
        return io::parse_timestamp_seconds(raw_timestamp);
    } catch (const std::exception&) {
        throw std::runtime_error("Forcing data error: invalid timestamp in column 'time' "
                                 "at row " +
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

double seconds_per_time_unit(std::string_view raw_units, std::string_view error_prefix) {
    // Normalize common fixed-duration BMI units to seconds
    const std::string units = to_lower_ascii(io::trim(std::string(raw_units)));
    if (units == "s" || units == "sec" || units == "second" || units == "seconds") {
        return 1.0;
    }
    if (units == "min" || units == "minute" || units == "minutes") {
        return 60.0;
    }
    if (units == "h" || units == "hr" || units == "hour" || units == "hours") {
        return 3600.0;
    }
    if (units == "d" || units == "day" || units == "days") {
        return 86400.0;
    }
    throw std::runtime_error(std::string(error_prefix) + "unsupported BMI time units '" +
                             std::string(raw_units) + "'");
}

void validate_forcing_time_step_delta(double actual_seconds, double expected_seconds,
                                      double tolerance_seconds, std::size_t record_number) {
    if (actual_seconds <= 0.0) {
        throw std::runtime_error(
            "Forcing data error: forcing timestamps must increase strictly at record " +
            std::to_string(record_number));
    }

    const double absolute_difference = std::abs(actual_seconds - expected_seconds);
    if (absolute_difference > tolerance_seconds) {
        std::ostringstream error;
        error.precision(std::numeric_limits<double>::max_digits10);
        error << "Forcing alignment error: forcing timestep " << actual_seconds
              << " s must match BMI time step " << expected_seconds << " s at record "
              << record_number;
        throw std::runtime_error(error.str());
    }
}

// Expose source metadata in its fixed reader order without format dispatch
const std::vector<BmiVariable>& ForcingReader::variables() const {
    return variables_;
}

// Initialize source metadata while constructing a concrete reader
void ForcingReader::initialize_variables(std::vector<BmiVariable> variables) {
    variables_ = std::move(variables);
}

CsvForcingReader::CsvForcingReader(const std::filesystem::path& forcing_path)
    : stream_(forcing_path) {
    if (!stream_.is_open()) {
        throw std::runtime_error("Forcing data error: unable to open CSV file '" +
                                 forcing_path.string() + "'");
    }

    // Resolve the time column and source variables from the CSV header
    std::string header_line;
    if (!std::getline(stream_, header_line)) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' is missing a header row");
    }

    const std::vector<std::string> headers = io::split_csv_line(header_line);
    const std::vector<std::string>::const_iterator time_it =
        std::find_if(headers.begin(), headers.end(), [](const std::string& header) {
            return header == "time" || header == "Time";
        });
    if (time_it == headers.end()) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must define a 'time' or 'Time' column");
    }
    time_column_index_ = static_cast<std::size_t>(std::distance(headers.begin(), time_it));

    // Align source column indices with the variable order fixed during construction
    std::vector<BmiVariable> variables;
    variables.reserve(headers.size() - 1);
    source_column_indices_.reserve(headers.size() - 1);
    for (std::size_t column_index = 0; column_index < headers.size(); ++column_index) {
        if (column_index == time_column_index_) {
            continue;
        }

        const std::string csv_header = io::trim(headers.at(column_index));
        if (csv_header.empty()) {
            throw std::runtime_error("Forcing data error: CSV file '" +
                                     forcing_path.string() +
                                     "' contains an empty data column header");
        }
        variables.emplace_back(csv_header, BmiValueType::bmi_double, "", "node", 0, 1);
        source_column_indices_.push_back(column_index);
    }

    if (variables.empty()) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must define at least one data column");
    }
    initialize_variables(std::move(variables));
}

double CsvForcingReader::time_seconds() const {
    return static_cast<double>(current_timestamp_seconds_);
}

bool CsvForcingReader::advance() {
    // Read until the next non-empty row so blank lines do not count as timesteps
    std::string row_line;
    while (std::getline(stream_, row_line)) {
        ++last_line_number_;
        if (io::trim(row_line).empty()) {
            continue;
        }

        const std::vector<std::string> cells = io::split_csv_line(row_line);
        if (time_column_index_ >= cells.size()) {
            throw std::runtime_error("Forcing data error: row " +
                                     std::to_string(last_line_number_) +
                                     " is missing column 'time'");
        }

        // Parse into local state so an invalid row cannot replace the current record
        const std::int64_t next_timestamp =
            parse_timestamp_seconds(cells.at(time_column_index_), last_line_number_);
        std::vector<double> next_values;
        next_values.reserve(source_column_indices_.size());

        // Preserve reader variable order in the current value buffer
        for (std::size_t variable_index = 0; variable_index < source_column_indices_.size();
             ++variable_index) {
            const std::size_t column_index = source_column_indices_.at(variable_index);
            const std::string& variable_name = variables().at(variable_index).name();
            if (column_index >= cells.size()) {
                throw std::runtime_error("Forcing data error: row " +
                                         std::to_string(last_line_number_) +
                                         " is missing column '" + variable_name + "'");
            }
            next_values.push_back(parse_numeric_value(cells.at(column_index), variable_name,
                                                      last_line_number_));
        }

        current_timestamp_seconds_ = next_timestamp;
        current_values_ = std::move(next_values);
        return true;
    }

    return false;
}

void CsvForcingReader::copy_value(std::size_t variable_index, void* destination) const {
    *static_cast<double*>(destination) = current_values_.at(variable_index);
}

ForcingBmi::ForcingBmi(::bmi::Bmi& target_model, double time_step_tolerance_seconds)
    : target_model_(target_model),
      time_step_tolerance_seconds_(time_step_tolerance_seconds) {}

void ForcingBmi::Initialize(std::string config_file) {
    // The config string for this internal module is the forcing CSV path
    const std::filesystem::path forcing_path = resolve_forcing_path(config_file);

    // Construct and prime the source before exposing forcing values
    time_step_seconds_ = get_model_time_step_seconds(target_model_);
    reader_ = std::make_unique<CsvForcingReader>(forcing_path);
    if (!reader_->advance()) {
        throw std::runtime_error("Forcing data error: CSV file '" + forcing_path.string() +
                                 "' must contain at least one data row");
    }

    exhausted_ = false;
}

void ForcingBmi::Update() {
    if (exhausted_) {
        ++step_;
        current_time_seconds_ += time_step_seconds_;
        return;
    }

    // Advance exactly one source record while retaining the previous timestamp
    const double previous_source_time = reader_->time_seconds();
    if (!reader_->advance()) {
        exhausted_ = true;
        ++step_;
        current_time_seconds_ += time_step_seconds_;
        return;
    }

    // Validate source alignment independently of its concrete storage format
    const double delta_seconds = reader_->time_seconds() - previous_source_time;
    validate_forcing_time_step_delta(delta_seconds, time_step_seconds_,
                                     time_step_tolerance_seconds_, step_ + 2);
    ++step_;
    current_time_seconds_ += time_step_seconds_;
}

void ForcingBmi::UpdateUntil(double /*time*/) {
    throw_unsupported("UpdateUntil");
}

void ForcingBmi::Finalize() {
    // Release the source at the end of this one-time lifecycle
    reader_.reset();
}

std::string ForcingBmi::GetComponentName() {
    return "ForcingBmi";
}

int ForcingBmi::GetInputItemCount() {
    return 0;
}

int ForcingBmi::GetOutputItemCount() {
    return reader_ ? static_cast<int>(reader_->variables().size()) : 0;
}

std::vector<std::string> ForcingBmi::GetInputVarNames() {
    return {};
}

std::vector<std::string> ForcingBmi::GetOutputVarNames() {
    std::vector<std::string> output_names;
    if (!reader_) {
        return output_names;
    }

    // Preserve reader variable order in the BMI output name list
    output_names.reserve(reader_->variables().size());
    for (const BmiVariable& variable : reader_->variables()) {
        output_names.push_back(variable.name());
    }
    return output_names;
}

int ForcingBmi::GetVarGrid(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    return reader_->variables().at(variable_index).grid_id();
}

std::string ForcingBmi::GetVarType(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    const BmiVariable& variable = reader_->variables().at(variable_index);
    return std::string(bmi_value_type_name(variable.type()));
}

std::string ForcingBmi::GetVarUnits(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    return reader_->variables().at(variable_index).units();
}

int ForcingBmi::GetVarItemsize(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    const BmiVariable& variable = reader_->variables().at(variable_index);
    return static_cast<int>(bmi_value_type_size(variable.type()));
}

int ForcingBmi::GetVarNbytes(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    const BmiVariable& variable = reader_->variables().at(variable_index);
    return static_cast<int>(bmi_value_type_size(variable.type()) * variable.item_count());
}

std::string ForcingBmi::GetVarLocation(std::string name) {
    const std::size_t variable_index = find_output_index(name);
    return reader_->variables().at(variable_index).location();
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
    reader_->copy_value(output_index, dest);
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
    if (reader_) {
        for (std::size_t variable_index = 0; variable_index < reader_->variables().size();
             ++variable_index) {
            if (reader_->variables()[variable_index].name() == name) {
                return variable_index;
            }
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
