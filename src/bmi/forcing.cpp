#include "bmi/internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/internal.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

namespace {

// Forcing configuration parsed from a user-provided config file
struct ForcingConfig {
    std::string source_type_;       // Forcing backend identifier, currently "csv"
    std::string source_path_;       // Absolute forcing CSV path after resolution
    std::string csv_column_;        // CSV column name used as the forcing series
    std::string bmi_input_var_;     // Coupled variable name exposed by ForcingBmi
    double time_step_size_{0.0};    // Forcing timestep size in declared time_units_
    std::string time_units_;        // Time units used for forcing timestep metadata
};

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
    config.csv_column_ = key_values.at("csv_column");
    config.bmi_input_var_ = key_values.at("bmi_input_var");
    config.time_step_size_ =
        parse_positive_time_step(key_values.at("time_step_size"),
                                 forcing_config_path);
    config.time_units_ = key_values.at("time_units");
    return config;
}

std::vector<double> load_forcing_values(const ForcingConfig& config) {
    try {
        return io::read_csv_numeric_column(config.source_path_,
                                           config.csv_column_);
    } catch (const std::exception& exception) {
        throw std::runtime_error(std::string("Forcing data error: ") +
                                 exception.what());
    }
}

[[noreturn]] void throw_unsupported(const std::string& operation) {
    throw std::runtime_error("ForcingBmi: " + operation + " not supported");
}

// BMI forcing source driven by a CSV column. Exposes a single scalar double
// output variable whose name matches the driver input to be coupled.
class ForcingBmi : public ::bmi::Bmi {
public:
    void Initialize(std::string config_file) override {
        config_ = read_forcing_config(config_file);
        values_ = load_forcing_values(config_);
        step_ = 0;
        current_time_ = 0.0;
    }

    void Update() override {
        ++step_;
        current_time_ += config_.time_step_size_;
    }

    void UpdateUntil(double /*time*/) override { throw_unsupported("UpdateUntil"); }

    void Finalize() override {
        values_.clear();
        values_.shrink_to_fit();
    }

    std::string GetComponentName() override { return "ForcingBmi"; }
    int GetInputItemCount() override { return 0; }
    int GetOutputItemCount() override { return 1; }
    std::vector<std::string> GetInputVarNames() override { return {}; }
    std::vector<std::string> GetOutputVarNames() override {
        return {config_.bmi_input_var_};
    }

    int GetVarGrid(std::string name) override {
        check_var_name(name);
        return 0;
    }
    std::string GetVarType(std::string name) override {
        check_var_name(name);
        return "double";
    }
    std::string GetVarUnits(std::string name) override {
        check_var_name(name);
        return "";
    }
    int GetVarItemsize(std::string name) override {
        check_var_name(name);
        return static_cast<int>(sizeof(double));
    }
    int GetVarNbytes(std::string name) override {
        check_var_name(name);
        return static_cast<int>(sizeof(double));
    }
    std::string GetVarLocation(std::string name) override {
        check_var_name(name);
        return "node";
    }

    double GetCurrentTime() override { return current_time_; }
    double GetStartTime() override { return 0.0; }
    double GetEndTime() override {
        return static_cast<double>(values_.size()) * config_.time_step_size_;
    }
    std::string GetTimeUnits() override { return config_.time_units_; }
    double GetTimeStep() override { return config_.time_step_size_; }

    void GetValue(std::string name, void* dest) override {
        check_var_name(name);
        if (step_ >= values_.size()) {
            throw std::runtime_error(
                "Forcing alignment error: forcing series exhausted at step " +
                std::to_string(step_));
        }
        *static_cast<double*>(dest) = values_.at(step_);
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
    void check_var_name(const std::string& name) const {
        if (name != config_.bmi_input_var_) {
            throw std::runtime_error(
                "ForcingBmi: unknown variable '" + name + "'");
        }
    }
    void check_grid_id(int grid) const {
        if (grid != 0) {
            throw std::runtime_error(
                "ForcingBmi: unknown grid " + std::to_string(grid));
        }
    }

    ForcingConfig config_;
    std::vector<double> values_;
    std::size_t step_{0};
    double current_time_{0.0};
};

// Current forcing contract supports one scalar double BMI input target
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

void validate_time_alignment(::bmi::Bmi& forcing, ::bmi::Bmi& model) {
    const std::string forcing_units = forcing.GetTimeUnits();
    const std::string model_units = model.GetTimeUnits();
    if (forcing_units != model_units) {
        throw std::runtime_error(
            "BMI forcing error: forcing time_units '" + forcing_units +
            "' must match BMI time units '" + model_units + "'");
    }

    const double model_time_step = model.GetTimeStep();
    if (!std::isfinite(model_time_step) || model_time_step <= 0.0) {
        throw std::runtime_error(
            "BMI forcing error: invalid BMI time step '" +
            std::to_string(model_time_step) + "'");
    }

    const double forcing_time_step = forcing.GetTimeStep();
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

std::unique_ptr<::bmi::Bmi> make_forcing_bmi() {
    return std::make_unique<ForcingBmi>();
}

void validate_forcing_coupling(::bmi::Bmi& forcing, ::bmi::Bmi& model) {
    const std::vector<std::string> out_names = forcing.GetOutputVarNames();
    if (out_names.size() != 1) {
        throw std::runtime_error(
            "BMI forcing error: forcing must expose exactly one output variable");
    }
    validate_driver_input(model, out_names.front());
    validate_time_alignment(forcing, model);
}

} // namespace miaow::bmi
