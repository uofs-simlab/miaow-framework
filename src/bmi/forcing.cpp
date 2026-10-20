#include "bmi/internal.hpp"

#include <algorithm>
#include <array>
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
#include <type_traits>
#include <utility>
#include <vector>

#include <netcdf.h>

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

// Convert a NetCDF status into a categorized error with operation context
void check_net_cdf(int status, const std::string& operation) {
    if (status != NC_NOERR) {
        throw std::runtime_error("Forcing data error: " + operation + ": " +
                                 nc_strerror(status));
    }
}

// Report whether a NetCDF type supports conversion through the numeric C API
bool is_net_cdf_numeric(nc_type type) {
    switch (type) {
    case NC_BYTE:
    case NC_SHORT:
    case NC_INT:
    case NC_FLOAT:
    case NC_DOUBLE:
    case NC_UBYTE:
    case NC_USHORT:
    case NC_UINT:
    case NC_INT64:
    case NC_UINT64:
        return true;
    default:
        return false;
    }
}

// Read an optional character attribute and trim surrounding whitespace
std::optional<std::string> read_net_cdf_text_attribute(int file_id, int variable_id,
                                                       const std::string& name) {
    nc_type type = NC_NAT;
    std::size_t size = 0;
    const int inquiry_status = nc_inq_att(file_id, variable_id, name.c_str(), &type, &size);
    if (inquiry_status == NC_ENOTATT) {
        return std::nullopt;
    }
    check_net_cdf(inquiry_status, "unable to inspect attribute '" + name + "'");
    if (type != NC_CHAR) {
        throw std::runtime_error("Forcing data error: attribute '" + name +
                                 "' must be text");
    }

    std::string value(size, '\0');
    check_net_cdf(nc_get_att_text(file_id, variable_id, name.c_str(), value.data()),
                  "unable to read attribute '" + name + "'");
    return io::trim(value);
}

// Read numeric markers in the same representation used for the variable values
template <typename Value>
std::vector<Value> read_net_cdf_numeric_attribute(
    int file_id, int variable_id, const std::string& name,
    int (*read_attribute)(int, int, const char*, Value*)) {
    nc_type type = NC_NAT;
    std::size_t size = 0;
    const int inquiry_status = nc_inq_att(file_id, variable_id, name.c_str(), &type, &size);
    if (inquiry_status == NC_ENOTATT) {
        return {};
    }
    check_net_cdf(inquiry_status, "unable to inspect attribute '" + name + "'");
    if (!is_net_cdf_numeric(type)) {
        throw std::runtime_error("Forcing data error: attribute '" + name +
                                 "' must be numeric");
    }

    // Fractional or out-of-range floating markers cannot equal an integer ID.
    // Filter them before conversion, since the integer API would truncate them.
    if constexpr (std::is_integral_v<Value>) {
        if (type == NC_FLOAT || type == NC_DOUBLE) {
            const std::vector<double> markers = read_net_cdf_numeric_attribute(
                file_id, variable_id, name, nc_get_att_double);
            const double upper_limit = std::ldexp(1.0, std::numeric_limits<Value>::digits);
            const double lower_limit = std::is_signed_v<Value> ? -upper_limit : 0.0;
            std::vector<Value> values;
            for (const double marker : markers) {
                if (std::isfinite(marker) && std::trunc(marker) == marker &&
                    marker >= lower_limit && marker < upper_limit) {
                    values.push_back(static_cast<Value>(marker));
                }
            }
            return values;
        }
    }

    std::vector<Value> values(size);
    check_net_cdf(read_attribute(file_id, variable_id, name.c_str(), values.data()),
                  "unable to read attribute '" + name + "'");
    return values;
}

// The fill inquiry writes the native variable type, without numeric conversion
template <typename Value>
std::optional<Value> read_net_cdf_fill_value(int file_id, int variable_id) {
    Value value{};
    int no_fill = 0;
    check_net_cdf(nc_inq_var_fill(file_id, variable_id, &no_fill, &value),
                  "unable to inspect the NetCDF fill value");
    // NetCDF-4 leaves the output untouched when filling is disabled. An explicit
    // _FillValue remains a missing-data marker even when allocation is not prefilled.
    if (no_fill) {
        const int status = nc_get_att(file_id, variable_id, "_FillValue", &value);
        if (status == NC_ENOTATT) {
            return std::nullopt;
        }
        check_net_cdf(status, "unable to read attribute '_FillValue'");
    }
    return value;
}

// Normalize native fill values only for variables read through the double API
std::optional<double> read_net_cdf_fill_value_as_double(int file_id, int variable_id) {
    nc_type type = NC_NAT;
    check_net_cdf(nc_inq_vartype(file_id, variable_id, &type),
                  "unable to inspect the NetCDF variable type");
    switch (type) {
    case NC_BYTE:
        return read_net_cdf_fill_value<signed char>(file_id, variable_id);
    case NC_SHORT:
        return read_net_cdf_fill_value<short>(file_id, variable_id);
    case NC_INT:
        return read_net_cdf_fill_value<int>(file_id, variable_id);
    case NC_FLOAT:
        return read_net_cdf_fill_value<float>(file_id, variable_id);
    case NC_DOUBLE:
        return read_net_cdf_fill_value<double>(file_id, variable_id);
    case NC_UBYTE:
        return read_net_cdf_fill_value<unsigned char>(file_id, variable_id);
    case NC_USHORT:
        return read_net_cdf_fill_value<unsigned short>(file_id, variable_id);
    case NC_UINT:
        return read_net_cdf_fill_value<unsigned int>(file_id, variable_id);
    case NC_INT64:
        return read_net_cdf_fill_value<long long>(file_id, variable_id);
    case NC_UINT64:
        return read_net_cdf_fill_value<unsigned long long>(file_id, variable_id);
    default:
        throw std::runtime_error("Forcing data error: NetCDF variable must be numeric");
    }
}

// Include the effective fill value, even when no _FillValue attribute was written
template <typename Value>
std::vector<Value> read_net_cdf_missing_values(
    int file_id, int variable_id, std::optional<Value> fill_value,
    int (*read_attribute)(int, int, const char*, Value*)) {
    std::vector<Value> values;
    if (fill_value) {
        values.push_back(*fill_value);
    }
    const std::vector<Value> missing = read_net_cdf_numeric_attribute(
        file_id, variable_id, "missing_value", read_attribute);
    values.insert(values.end(), missing.begin(), missing.end());
    return values;
}

std::vector<double> read_net_cdf_missing_values(int file_id, int variable_id) {
    return read_net_cdf_missing_values(
        file_id, variable_id, read_net_cdf_fill_value_as_double(file_id, variable_id),
        nc_get_att_double);
}

// Compare markers before integer identifiers can lose precision through conversion
template <typename Value>
bool is_missing_net_cdf_value(Value value, const std::vector<Value>& missing_values) {
    return std::any_of(missing_values.begin(), missing_values.end(),
                       [value](Value missing) { return value == missing; });
}

// Raw NetCDF reads do not apply the packing convention to stored values
void reject_net_cdf_packing(int file_id, int variable_id) {
    for (const char* attribute : {"scale_factor", "add_offset"}) {
        const int status = nc_inq_att(file_id, variable_id, attribute, nullptr, nullptr);
        if (status == NC_ENOTATT) {
            continue;
        }
        check_net_cdf(status,
                      "unable to inspect attribute '" + std::string(attribute) + "'");
        std::array<char, NC_MAX_NAME + 1> name{};
        check_net_cdf(nc_inq_varname(file_id, variable_id, name.data()),
                      "unable to inspect NetCDF variable name");
        throw std::runtime_error("Forcing data error: packed NetCDF variable '" +
                                 std::string(name.data()) + "' uses '" + attribute +
                                 "'; scale_factor and add_offset are not supported");
    }
}

// Find one time coordinate using axis, standard name, and variable name precedence
int find_net_cdf_time_variable(int file_id) {
    int variable_count = 0;
    check_net_cdf(nc_inq_nvars(file_id, &variable_count), "unable to inspect variables");

    std::vector<int> axis_candidates;
    std::vector<int> standard_name_candidates;
    std::vector<int> named_candidates;
    for (int variable_id = 0; variable_id < variable_count; ++variable_id) {
        std::array<char, NC_MAX_NAME + 1> variable_name{};
        nc_type type = NC_NAT;
        int rank = 0;
        check_net_cdf(nc_inq_var(file_id, variable_id, variable_name.data(), &type, &rank,
                                 nullptr, nullptr),
                      "unable to inspect variable");
        if (rank != 1 || !is_net_cdf_numeric(type)) {
            continue;
        }

        const std::optional<std::string> axis =
            read_net_cdf_text_attribute(file_id, variable_id, "axis");
        if (axis && *axis == "T") {
            axis_candidates.push_back(variable_id);
        }
        const std::optional<std::string> standard_name =
            read_net_cdf_text_attribute(file_id, variable_id, "standard_name");
        if (standard_name && *standard_name == "time") {
            standard_name_candidates.push_back(variable_id);
        }
        if (std::string(variable_name.data()) == "time") {
            named_candidates.push_back(variable_id);
        }
    }

    const auto select_candidate = [](const std::vector<int>& candidates) {
        if (candidates.size() > 1) {
            throw std::runtime_error(
                "Forcing data error: NetCDF time coordinate is ambiguous");
        }
        return candidates.empty() ? -1 : candidates.front();
    };
    if (!axis_candidates.empty()) {
        return select_candidate(axis_candidates);
    }
    if (!standard_name_candidates.empty()) {
        return select_candidate(standard_name_candidates);
    }
    if (!named_candidates.empty()) {
        return select_candidate(named_candidates);
    }
    throw std::runtime_error("Forcing data error: NetCDF time coordinate was not found");
}

// Parse the fixed unit before "since" and return its scale in seconds
double net_cdf_time_unit_seconds(int file_id, int time_variable_id) {
    const std::optional<std::string> units =
        read_net_cdf_text_attribute(file_id, time_variable_id, "units");
    if (!units) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time coordinate requires units");
    }

    const std::string lower_units = to_lower_ascii(*units);
    const std::size_t since_position = lower_units.find("since");
    if (since_position == std::string::npos ||
        io::trim(lower_units.substr(0, since_position)).empty() ||
        io::trim(lower_units.substr(since_position + 5)).empty()) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time units must contain '<unit> since "
            "<reference>'");
    }
    return seconds_per_time_unit(lower_units.substr(0, since_position),
                                 "Forcing data error: ");
}

// Validate and format one numeric identifier as a signed decimal integer
std::string net_cdf_feature_id(double value) {
    constexpr double signed_integer_limit = 9223372036854775808.0;
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < -signed_integer_limit || value >= signed_integer_limit) {
        throw std::runtime_error(
            "Forcing data error: NetCDF feature identifiers must be finite "
            "signed 64-bit integers");
    }
    return std::to_string(static_cast<std::int64_t>(value));
}

std::string net_cdf_feature_id(long long value) {
    return std::to_string(value);
}

std::string net_cdf_feature_id(unsigned long long value) {
    if (value > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error(
            "Forcing data error: NetCDF feature identifiers must be finite "
            "signed 64-bit integers");
    }
    return std::to_string(value);
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
    throw std::runtime_error(std::string(error_prefix) + "unsupported time units '" +
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

NetCdfFile::NetCdfFile(const std::filesystem::path& path) {
    const int status = nc_open(path.string().c_str(), NC_NOWRITE, &id_);
    if (status != NC_NOERR) {
        id_ = -1;
        check_net_cdf(status, "unable to open NetCDF file '" + path.string() + "'");
    }
}

NetCdfFile::~NetCdfFile() {
    if (id_ >= 0) {
        nc_close(id_);
    }
}

int NetCdfFile::id() const {
    return id_;
}

NetCdfForcingReader::NetCdfForcingReader(const std::filesystem::path& forcing_path,
                                         const std::string& feature_id_variable,
                                         const std::string& feature_id)
    : file_(forcing_path), feature_id_(feature_id) {
    // Resolve the configured identifier variable and use its sole dimension as the feature
    // dimension
    int feature_variable_id = -1;
    check_net_cdf(nc_inq_varid(file_.id(), feature_id_variable.c_str(),
                               &feature_variable_id),
                  "unable to find feature identifier variable '" + feature_id_variable +
                      "'");

    std::array<int, NC_MAX_VAR_DIMS> feature_dimension_ids{};
    nc_type feature_type = NC_NAT;
    int feature_rank = 0;
    check_net_cdf(nc_inq_var(file_.id(), feature_variable_id, nullptr, &feature_type,
                             &feature_rank, feature_dimension_ids.data(), nullptr),
                  "unable to inspect feature identifier variable '" + feature_id_variable +
                      "'");
    if (feature_rank != 1 || !is_net_cdf_numeric(feature_type)) {
        throw std::runtime_error("Forcing data error: feature identifier variable '" +
                                 feature_id_variable +
                                 "' must be numeric and one-dimensional");
    }
    reject_net_cdf_packing(file_.id(), feature_variable_id);
    const int feature_dimension_id = feature_dimension_ids.front();

    std::size_t feature_count = 0;
    check_net_cdf(nc_inq_dimlen(file_.id(), feature_dimension_id, &feature_count),
                  "unable to inspect the feature dimension");
    if (feature_count == 0) {
        throw std::runtime_error(
            "Forcing data error: NetCDF feature dimension must not be empty");
    }

    // Preserve integer identifiers and their missing markers without passing through double
    std::size_t match_count = 0;
    const auto find_feature = [&](auto read_values, auto read_attribute, auto fill_value) {
        using Value = typename decltype(fill_value)::value_type;
        std::vector<Value> feature_values(feature_count);
        check_net_cdf(read_values(file_.id(), feature_variable_id, feature_values.data()),
                      "unable to read feature identifiers");
        const std::vector<Value> missing_values = read_net_cdf_missing_values(
            file_.id(), feature_variable_id, fill_value, read_attribute);
        for (std::size_t index = 0; index < feature_values.size(); ++index) {
            const Value value = feature_values.at(index);
            if (is_missing_net_cdf_value(value, missing_values)) {
                throw std::runtime_error(
                    "Forcing data error: feature identifier variable '" +
                    feature_id_variable + "' contains a missing value");
            }
            if (net_cdf_feature_id(value) == feature_id_) {
                feature_index_ = index;
                ++match_count;
            }
        }
    };
    if (feature_type == NC_FLOAT || feature_type == NC_DOUBLE) {
        find_feature(nc_get_var_double, nc_get_att_double,
                     read_net_cdf_fill_value_as_double(file_.id(), feature_variable_id));
    } else if (feature_type == NC_UINT64) {
        find_feature(nc_get_var_ulonglong, nc_get_att_ulonglong,
                     read_net_cdf_fill_value<unsigned long long>(file_.id(),
                                                                feature_variable_id));
    } else {
        const std::optional<long long> fill_value =
            feature_type == NC_INT64
                ? read_net_cdf_fill_value<long long>(file_.id(), feature_variable_id)
                : std::optional<long long>(
                      read_net_cdf_fill_value_as_double(file_.id(), feature_variable_id));
        find_feature(nc_get_var_longlong, nc_get_att_longlong, fill_value);
    }
    if (match_count == 0) {
        throw std::runtime_error("Forcing data error: feature identifier variable '" +
                                 feature_id_variable + "' does not contain feature '" +
                                 feature_id_ + "'");
    }
    if (match_count > 1) {
        throw std::runtime_error("Forcing data error: feature identifier variable '" +
                                 feature_id_variable + "' repeats feature '" + feature_id_ +
                                 "'");
    }

    // Detect the time coordinate and retain the dimension used for scalar reads
    time_variable_id_ = find_net_cdf_time_variable(file_.id());
    reject_net_cdf_packing(file_.id(), time_variable_id_);
    time_missing_values_ = read_net_cdf_missing_values(file_.id(), time_variable_id_);
    int time_dimension_id = -1;
    check_net_cdf(nc_inq_vardimid(file_.id(), time_variable_id_, &time_dimension_id),
                  "unable to inspect the NetCDF time coordinate");
    if (time_dimension_id == feature_dimension_id) {
        throw std::runtime_error(
            "Forcing data error: time and feature dimensions must be distinct");
    }

    // Establish the time extent and conversion used to normalize records to seconds
    check_net_cdf(nc_inq_dimlen(file_.id(), time_dimension_id, &time_size_),
                  "unable to inspect the time dimension");
    if (time_size_ == 0) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time dimension must not be empty");
    }
    time_unit_seconds_ = net_cdf_time_unit_seconds(file_.id(), time_variable_id_);

    // Retain the first coordinate as the zero point for elapsed forcing time
    const std::size_t first_index = 0;
    check_net_cdf(nc_get_var1_double(file_.id(), time_variable_id_, &first_index,
                                     &first_time_),
                  "unable to read the first NetCDF time coordinate");
    if (!std::isfinite(first_time_)) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time coordinate must be finite");
    }
    if (is_missing_net_cdf_value(first_time_, time_missing_values_)) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time coordinate contains a missing value");
    }

    // Select numeric variables with exactly the time and feature dimensions; retain their
    // dimension order for later scalar hyperslab reads
    int variable_count = 0;
    check_net_cdf(nc_inq_nvars(file_.id(), &variable_count), "unable to inspect variables");
    std::vector<BmiVariable> variables;
    for (int variable_id = 0; variable_id < variable_count; ++variable_id) {
        std::array<char, NC_MAX_NAME + 1> variable_name{};
        std::array<int, NC_MAX_VAR_DIMS> dimension_ids{};
        nc_type type = NC_NAT;
        int rank = 0;
        check_net_cdf(nc_inq_var(file_.id(), variable_id, variable_name.data(), &type,
                                 &rank, dimension_ids.data(), nullptr),
                      "unable to inspect NetCDF variable");
        if (rank != 2 || !is_net_cdf_numeric(type)) {
            continue;
        }

        const bool time_first = dimension_ids.at(0) == time_dimension_id &&
                                dimension_ids.at(1) == feature_dimension_id;
        const bool feature_first = dimension_ids.at(0) == feature_dimension_id &&
                                   dimension_ids.at(1) == time_dimension_id;
        if (!time_first && !feature_first) {
            continue;
        }

        reject_net_cdf_packing(file_.id(), variable_id);
        const std::optional<std::string> units =
            read_net_cdf_text_attribute(file_.id(), variable_id, "units");
        variables.emplace_back(variable_name.data(), BmiValueType::bmi_double,
                               units.value_or(""), "node", 0, 1);
        variable_ids_.push_back(variable_id);
        time_dimension_first_.push_back(time_first);
        missing_values_.push_back(read_net_cdf_missing_values(file_.id(), variable_id));
    }
    if (variables.empty()) {
        throw std::runtime_error(
            "Forcing data error: NetCDF file contains no numeric variables with "
            "the time and feature dimensions");
    }
    current_values_.resize(variables.size());
    initialize_variables(std::move(variables));
}

double NetCdfForcingReader::time_seconds() const {
    return current_time_seconds_;
}

bool NetCdfForcingReader::advance() {
    // Stop cleanly when every available time record has been consumed
    if (next_time_index_ >= time_size_) {
        return false;
    }

    // Read the next source time and reject values that cannot define a finite delta
    double source_time = 0.0;
    check_net_cdf(nc_get_var1_double(file_.id(), time_variable_id_, &next_time_index_,
                                     &source_time),
                  "unable to read NetCDF time coordinate");
    if (!std::isfinite(source_time)) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time coordinate must be finite");
    }
    if (is_missing_net_cdf_value(source_time, time_missing_values_)) {
        throw std::runtime_error(
            "Forcing data error: NetCDF time coordinate contains a missing value");
    }

    // Read one scalar per variable at the selected feature and current time index
    for (std::size_t variable_index = 0; variable_index < variable_ids_.size();
         ++variable_index) {
        const std::array<std::size_t, 2> start =
            time_dimension_first_.at(variable_index)
                ? std::array<std::size_t, 2>{next_time_index_, feature_index_}
                : std::array<std::size_t, 2>{feature_index_, next_time_index_};
        constexpr std::array<std::size_t, 2> count{1, 1};
        double value = 0.0;
        check_net_cdf(nc_get_vara_double(file_.id(), variable_ids_.at(variable_index),
                                         start.data(), count.data(), &value),
                      "unable to read NetCDF forcing variable '" +
                          variables().at(variable_index).name() + "'");

        // Reject missing source data before it can be exposed through BMI
        if (!std::isfinite(value) ||
            is_missing_net_cdf_value(value, missing_values_.at(variable_index))) {
            throw std::runtime_error("Forcing data error: NetCDF variable '" +
                                     variables().at(variable_index).name() +
                                     "' contains a missing value for "
                                     "feature '" +
                                     feature_id_ + "' at time index " +
                                     std::to_string(next_time_index_));
        }
        current_values_.at(variable_index) = value;
    }

    // Convert the source offset to elapsed seconds relative to the first record
    current_time_seconds_ = (source_time - first_time_) * time_unit_seconds_;
    ++next_time_index_;
    return true;
}

void NetCdfForcingReader::copy_value(std::size_t variable_index, void* destination) const {
    *static_cast<double*>(destination) = current_values_.at(variable_index);
}

ForcingBmi::ForcingBmi(::bmi::Bmi& target_model, BmiDataFormat format,
                       std::optional<std::string> feature_id_variable,
                       std::string feature_id, double time_step_tolerance_seconds)
    : target_model_(target_model), format_(format),
      feature_id_variable_(std::move(feature_id_variable)),
      feature_id_(std::move(feature_id)),
      time_step_tolerance_seconds_(time_step_tolerance_seconds) {}

void ForcingBmi::Initialize(std::string config_file) {
    // The config string for this internal module is the forcing source path
    const std::filesystem::path forcing_path = resolve_forcing_path(config_file);

    // Construct and prime the source before exposing forcing values
    time_step_seconds_ = get_model_time_step_seconds(target_model_);
    if (format_ == BmiDataFormat::netcdf) {
        if (!feature_id_variable_ || io::trim(*feature_id_variable_).empty() ||
            io::trim(feature_id_).empty()) {
            throw std::runtime_error(
                "Forcing config error: NetCDF forcing requires a feature identifier "
                "variable and task feature id");
        }
        reader_ = std::make_unique<NetCdfForcingReader>(forcing_path, *feature_id_variable_,
                                                        feature_id_);
    } else {
        reader_ = std::make_unique<CsvForcingReader>(forcing_path);
    }
    if (!reader_->advance()) {
        throw std::runtime_error("Forcing data error: forcing file '" +
                                 forcing_path.string() +
                                 "' must contain at least one data record");
    }

    step_ = 0;
    current_time_seconds_ = 0.0;
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
    // The end time is unknown without scanning the remaining source records
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
