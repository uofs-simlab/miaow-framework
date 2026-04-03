#include "bmi/internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/csv.hpp"
#include "io/utils.hpp"

namespace miaow::bmi {

namespace {

// Current forcing contract supports one scalar double BMI input target
void validate_forcing_target(::bmi::Bmi& model, const std::string& bmi_input_var) {
    const std::vector<std::string> input_vars = model.GetInputVarNames();
    if (std::find(input_vars.begin(), input_vars.end(), bmi_input_var) ==
        input_vars.end()) {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + bmi_input_var +
            "' not found");
    }

    const std::string var_type = model.GetVarType(bmi_input_var);
    if (var_type != "double") {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + bmi_input_var +
            "' must have BMI type 'double' but has '" + var_type + "'");
    }

    const int n_bytes = model.GetVarNbytes(bmi_input_var);
    if (n_bytes != static_cast<int>(sizeof(double))) {
        throw std::runtime_error(
            "BMI forcing error: input variable '" + bmi_input_var +
            "' must be scalar (8 bytes) but has " + std::to_string(n_bytes) +
            " byte(s)");
    }
}

double parse_positive_time_step(const std::string& raw_value,
                                const std::string& forcing_config_path) {
    try {
        std::size_t parsed_chars = 0;
        const double parsed_value = std::stod(raw_value, &parsed_chars);
        if (parsed_chars != raw_value.size() ||
            !std::isfinite(parsed_value) ||
            parsed_value <= 0.0) {
            throw std::runtime_error(
                "Forcing config error: invalid time_step_size '" + raw_value +
                "' in '" + forcing_config_path + "'");
        }
        return parsed_value;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Forcing config error: invalid time_step_size '" + raw_value +
            "' in '" + forcing_config_path + "'");
    }
}

void validate_forcing_timestep(::bmi::Bmi& model,
                               const ForcingConfig& forcing_config) {
    const std::string model_time_units = model.GetTimeUnits();
    if (forcing_config.time_units_ != model_time_units) {
        throw std::runtime_error(
            "BMI forcing error: forcing time_units '" + forcing_config.time_units_ +
            "' must match BMI time units '" + model_time_units + "'");
    }

    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error(
            "BMI forcing error: invalid BMI time step '" +
            std::to_string(model_time_step) + "'");
    }

    const double forcing_time_step = forcing_config.time_step_size_;
    const double absolute_difference =
        std::abs(forcing_time_step - model_time_step);
    const double tolerance = std::max(
        1e-12,
        std::numeric_limits<double>::epsilon() *
            std::max(std::abs(forcing_time_step), std::abs(model_time_step)) *
            16.0);

    if (absolute_difference > tolerance) {
        std::ostringstream error;
        error.precision(std::numeric_limits<double>::max_digits10);
        error << "BMI forcing error: forcing time_step_size "
              << forcing_time_step << " must match BMI time step "
              << model_time_step;
        throw std::runtime_error(error.str());
    }
}

} // namespace

ForcingConfig read_forcing_config(const std::string& forcing_config_path) {
    std::ifstream config_stream(forcing_config_path);
    if (!config_stream.is_open()) {
        throw std::runtime_error(
            "Forcing config error: unable to open forcing config file '" +
            forcing_config_path + "'");
    }

    std::map<std::string, std::string> key_values;
    std::size_t line_number = 0;
    std::string line;
    while (std::getline(config_stream, line)) {
        ++line_number;
        const std::string trimmed_line = io::trim(line);
        if (trimmed_line.empty() || trimmed_line.rfind("#", 0) == 0) {
            continue;
        }

        const std::size_t equals_pos = trimmed_line.find('=');
        if (equals_pos == std::string::npos) {
            throw std::runtime_error(
                "Forcing config error: invalid line " +
                std::to_string(line_number) + " in '" +
                forcing_config_path + "'");
        }

        const std::string key = io::trim(trimmed_line.substr(0, equals_pos));
        const std::string value = io::trim(trimmed_line.substr(equals_pos + 1));
        if (key.empty()) {
            throw std::runtime_error(
                "Forcing config error: empty key at line " +
                std::to_string(line_number) + " in '" + forcing_config_path +
                "'");
        }
        if (key_values.find(key) != key_values.end()) {
            throw std::runtime_error(
                "Forcing config error: duplicate key '" + key + "' in '" +
                forcing_config_path + "'");
        }
        key_values.emplace(key, value);
    }

    const std::vector<std::string> required_keys{
        "source_type",
        "source_path",
        "subdomain_id",
        "csv_column",
        "bmi_input_var",
        "time_step_size",
        "time_units",
    };
    for (const std::string& key : required_keys) {
        const auto it = key_values.find(key);
        if (it == key_values.end() || it->second.empty()) {
            throw std::runtime_error(
                "Forcing config error: missing key '" + key + "' in '" +
                forcing_config_path + "'");
        }
    }

    const std::string source_type = key_values.at("source_type");
    if (source_type != "csv") {
        throw std::runtime_error(
            "Forcing config error: unsupported source_type '" + source_type +
            "'");
    }

    std::filesystem::path source_path = key_values.at("source_path");
    if (source_path.is_relative()) {
        source_path = std::filesystem::path(forcing_config_path).parent_path() /
                      source_path;
    }

    ForcingConfig config;
    config.source_type_ = source_type;
    config.source_path_ = source_path.lexically_normal().string();
    config.subdomain_id_ = key_values.at("subdomain_id");
    config.csv_column_ = key_values.at("csv_column");
    config.bmi_input_var_ = key_values.at("bmi_input_var");
    config.time_step_size_ =
        parse_positive_time_step(key_values.at("time_step_size"),
                                 forcing_config_path);
    config.time_units_ = key_values.at("time_units");
    return config;
}

ForcingData load_forcing_data(::bmi::Bmi& model,
                              const ForcingConfig& forcing_config) {
    validate_forcing_target(model, forcing_config.bmi_input_var_);
    validate_forcing_timestep(model, forcing_config);

    ForcingData forcing_data;
    forcing_data.bmi_input_var_ = forcing_config.bmi_input_var_;
    try {
        forcing_data.values_ = io::read_csv_numeric_column(
            forcing_config.source_path_, forcing_config.csv_column_);
    } catch (const std::exception& exception) {
        throw std::runtime_error(std::string("Forcing data error: ") +
                                 exception.what());
    }
    return forcing_data;
}

} // namespace miaow::bmi
