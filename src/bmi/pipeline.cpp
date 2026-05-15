#include "miaow_bmi.hpp"
#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <caf/json.hpp>

namespace miaow::bmi {

// Construct a BMI pipeline from the supplied model
BmiPipeline::BmiPipeline(std::unique_ptr<::bmi::Bmi> supplied_model)
    : supplied_model_(std::move(supplied_model)) {}

void BmiPipeline::throw_config_error(const std::string& message) const {
    io::throw_config_error("BMI pipeline config error: ", message);
}

// Parse the single bmi_multi formulation used by the current pipeline schema
caf::json_object BmiPipeline::read_bmi_multi_params(const caf::json_object& global) const {
    const caf::json_array formulations = global.value("formulations").to_array();
    if (formulations.size() != 1) {
        throw_config_error("BMI pipeline config formulations must contain one entry");
    }
    const caf::json_object formulation = (*formulations.begin()).to_object();
    const std::string formulation_name =
        io::trim(std::string(formulation.value("name").to_string()));
    if (formulation_name != "bmi_multi") {
        throw_config_error("unsupported formulation '" + formulation_name + "'");
    }
    return formulation.value("params").to_object();
}

// The BMI pipeline currently supports a single module per bmi_multi formulation
BmiModuleConfig
BmiPipeline::parse_single_module_config(const caf::json_object& bmi_multi_params) const {
    const caf::json_array modules = bmi_multi_params.value("modules").to_array();

    // The current pipeline schema supports one model module
    if (modules.size() != 1) {
        throw_config_error("bmi_multi modules must contain one entry");
    }

    return BmiModuleConfig((*modules.begin()).to_object(), "BMI pipeline config error: ");
}

// Add a model module to the pipeline and return its module index
std::size_t BmiPipeline::add_model_module(const BmiModuleConfig& module_config) {
    if (!module_config.dynamic_backend_) {
        if (!supplied_model_) {
            throw_config_error("module 'bmi_model' requires a supplied BMI model");
        }
        modules_.push_back(supplied_model_.get());
    } else {
        owned_modules_.push_back(make_dynamic_bmi_model(module_config));
        modules_.push_back(owned_modules_.back().get());
    }

    configs_.push_back(module_config.config_file_);
    return modules_.size() - 1;
}

// Add the forcing module and return whether forcing is active
bool BmiPipeline::add_forcing_module(const caf::json_object& global,
                                     std::size_t model_module,
                                     std::size_t& forcing_module) {
    std::string forcing_file;
    std::size_t clock_module = model_module;
    const caf::json_value forcing_value = global.value("forcing");
    const bool has_forcing = !forcing_value.is_undefined();
    if (has_forcing) {
        const caf::json_object forcing = forcing_value.to_object();
        const std::string provider =
            io::trim(std::string(forcing.value("provider").to_string()));
        if (provider != "CsvFile") {
            throw_config_error("unsupported forcing provider '" + provider + "'");
        }
        forcing_file = io::trim(std::string(forcing.value("file").to_string()));
        clock_module = static_cast<std::size_t>(forcing.value("clock_module").to_integer());
        if (clock_module >= modules_.size()) {
            throw std::runtime_error("BMI pipeline error: forcing clock module "
                                     "is invalid");
        }
    }

    // The forcing module is always last; empty config makes it a no-op
    forcing_module = modules_.size();
    owned_modules_.push_back(std::make_unique<ForcingBmi>(*modules_.at(clock_module)));
    modules_.push_back(owned_modules_.back().get());
    configs_.push_back(forcing_file);
    return has_forcing;
}

// Clear pipeline state to prepare for the next pipeline config initialization
void BmiPipeline::reset_pipeline() {
    owned_modules_.clear();
    modules_.clear();
    configs_.clear();
    couplings_.clear();
    output_module_ = 0;
    initialized_count_ = 0;
}

// Build scalar couplings from source outputs to target inputs
void BmiPipeline::build_couplings(
    std::size_t source_module, std::size_t target_module,
    const std::map<std::string, std::string>& variables_names_map) {
    ::bmi::Bmi& source_model = *modules_.at(source_module);
    ::bmi::Bmi& target_model = *modules_.at(target_module);

    const std::vector<std::string> source_outputs = source_model.GetOutputVarNames();
    const std::vector<std::string> target_inputs = target_model.GetInputVarNames();

    const auto append_validated_coupling = [&](const std::string& source_variable,
                                               const std::string& target_variable) {
        // Source variables must be available as BMI outputs
        const std::vector<std::string>::const_iterator source_it =
            std::find(source_outputs.begin(), source_outputs.end(), source_variable);
        if (source_it == source_outputs.end()) {
            throw std::runtime_error("BMI coupling error: source variable '" +
                                     source_variable + "' not found");
        }

        // Target variables must be available as BMI inputs
        const std::vector<std::string>::const_iterator target_it =
            std::find(target_inputs.begin(), target_inputs.end(), target_variable);
        if (target_it == target_inputs.end()) {
            throw std::runtime_error("BMI coupling error: target variable '" +
                                     target_variable + "' not found");
        }

        // Scalar couplings target one float or double value
        const std::string target_type = target_model.GetVarType(target_variable);
        int expected_n_bytes = 0;
        if (target_type == "double") {
            expected_n_bytes = static_cast<int>(sizeof(double));
        } else if (target_type == "float") {
            expected_n_bytes = static_cast<int>(sizeof(float));
        } else {
            throw std::runtime_error(
                "BMI coupling error: target variable '" + target_variable +
                "' must have BMI type 'double' or 'float' but has '" + target_type + "'");
        }

        const int n_bytes = target_model.GetVarNbytes(target_variable);
        if (n_bytes != expected_n_bytes) {
            throw std::runtime_error("BMI coupling error: target variable '" +
                                     target_variable + "' must be scalar (" +
                                     std::to_string(expected_n_bytes) + " bytes) but has " +
                                     std::to_string(n_bytes) + " byte(s)");
        }

        couplings_.push_back(
            BmiCoupling{source_module, source_variable, target_module, target_variable});
    };

    if (variables_names_map.empty()) {
        // Empty maps use same-named forcing headers for every model input
        for (const std::string& raw_input : target_inputs) {
            const std::string input_name = io::trim(raw_input);
            if (input_name.empty()) {
                throw std::runtime_error("BMI forcing error: identity variables_names_map "
                                         "encountered an empty BMI input variable name");
            }
            append_validated_coupling(input_name, input_name);
        }
        return;
    }

    // Explicit maps couple CSV headers to configured BMI input variables
    for (const std::pair<const std::string, std::string>& map_entry : variables_names_map) {
        append_validated_coupling(map_entry.second, map_entry.first);
    }
}

void BmiPipeline::Initialize(std::string config_file) {
    // Reset any previous BMI lifecycle before reading the next pipeline config
    if (initialized_count_ > 0) {
        Finalize();
    }
    reset_pipeline();

    // Read the top-level pipeline config and select the bmi_multi formulation
    const caf::json_object root = read_bmi_pipeline_config_file(config_file);
    const caf::json_object global = root.value("global").to_object();
    const caf::json_object bmi_multi_params = read_bmi_multi_params(global);
    output_module_ =
        static_cast<std::size_t>(bmi_multi_params.value("output_module").to_integer());

    // Add the configured model module before attaching optional forcing
    const BmiModuleConfig module_config = parse_single_module_config(bmi_multi_params);
    const std::size_t model_module = add_model_module(module_config);

    if (output_module_ >= modules_.size()) {
        throw std::runtime_error("BMI pipeline error: output module is invalid");
    }

    std::size_t forcing_module = 0;
    const bool has_forcing = add_forcing_module(global, model_module, forcing_module);

    // Initialize modules in update order so downstream metadata is available
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index) {
        modules_.at(module_index)->Initialize(configs_.at(module_index));
        ++initialized_count_;
    }

    if (has_forcing) {
        // Couplings are built after initialization so BMI metadata is available
        build_couplings(forcing_module, model_module, module_config.variables_names_map_);
    }
}

void BmiPipeline::apply_coupling(const BmiCoupling& coupling) {
    double source_value = 0.0;
    modules_.at(coupling.source_module_)
        ->GetValue(coupling.source_variable_, &source_value);

    // The target type decides whether SetValue receives a double* or float*
    const std::string target_type =
        modules_.at(coupling.target_module_)->GetVarType(coupling.target_variable_);

    if (target_type == "double") {
        modules_.at(coupling.target_module_)
            ->SetValue(coupling.target_variable_, &source_value);
        return;
    }

    if (target_type == "float") {
        float target_value = static_cast<float>(source_value);
        modules_.at(coupling.target_module_)
            ->SetValue(coupling.target_variable_, &target_value);
        return;
    }

    throw std::runtime_error("BMI pipeline error: unsupported BMI variable type '" +
                             target_type + "' for input variable '" +
                             coupling.target_variable_ + "'");
}

void BmiPipeline::Update() {
    // Each module receives its coupled inputs immediately before it advances
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index) {
        for (const BmiCoupling& coupling : couplings_) {
            if (coupling.target_module_ == module_index) {
                apply_coupling(coupling);
            }
        }
        modules_.at(module_index)->Update();
    }
}

void BmiPipeline::UpdateUntil(double time) {
    // Repeated pipeline updates preserve coupling order for each step
    while (GetCurrentTime() < time) {
        Update();
    }
}

void BmiPipeline::Finalize() {
    std::exception_ptr first_exception;

    // Finalize modules in update order so lifecycle order is explicit
    for (std::size_t module_index = 0; module_index < initialized_count_; ++module_index) {
        try {
            modules_.at(module_index)->Finalize();
        } catch (...) {
            if (!first_exception) {
                first_exception = std::current_exception();
            }
        }
    }

    // Mark the schedule as finalized while preserving module ownership
    initialized_count_ = 0;

    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

// The pipeline exposes one selected module as its BMI surface
::bmi::Bmi& BmiPipeline::output_model() {
    return *modules_.at(output_module_);
}

std::string BmiPipeline::GetComponentName() {
    return output_model().GetComponentName();
}

int BmiPipeline::GetInputItemCount() {
    return output_model().GetInputItemCount();
}

int BmiPipeline::GetOutputItemCount() {
    return output_model().GetOutputItemCount();
}

std::vector<std::string> BmiPipeline::GetInputVarNames() {
    return output_model().GetInputVarNames();
}

std::vector<std::string> BmiPipeline::GetOutputVarNames() {
    return output_model().GetOutputVarNames();
}

int BmiPipeline::GetVarGrid(std::string name) {
    return output_model().GetVarGrid(name);
}

std::string BmiPipeline::GetVarType(std::string name) {
    return output_model().GetVarType(name);
}

std::string BmiPipeline::GetVarUnits(std::string name) {
    return output_model().GetVarUnits(name);
}

int BmiPipeline::GetVarItemsize(std::string name) {
    return output_model().GetVarItemsize(name);
}

int BmiPipeline::GetVarNbytes(std::string name) {
    return output_model().GetVarNbytes(name);
}

std::string BmiPipeline::GetVarLocation(std::string name) {
    return output_model().GetVarLocation(name);
}

double BmiPipeline::GetCurrentTime() {
    return output_model().GetCurrentTime();
}

double BmiPipeline::GetStartTime() {
    return output_model().GetStartTime();
}

double BmiPipeline::GetEndTime() {
    return output_model().GetEndTime();
}

std::string BmiPipeline::GetTimeUnits() {
    return output_model().GetTimeUnits();
}

double BmiPipeline::GetTimeStep() {
    return output_model().GetTimeStep();
}

void BmiPipeline::GetValue(std::string name, void* dest) {
    output_model().GetValue(name, dest);
}

void* BmiPipeline::GetValuePtr(std::string name) {
    return output_model().GetValuePtr(name);
}

void BmiPipeline::GetValueAtIndices(std::string name, void* dest, int* inds, int count) {
    output_model().GetValueAtIndices(name, dest, inds, count);
}

void BmiPipeline::SetValue(std::string name, void* src) {
    output_model().SetValue(name, src);
}

void BmiPipeline::SetValueAtIndices(std::string name, int* inds, int count, void* src) {
    output_model().SetValueAtIndices(name, inds, count, src);
}

int BmiPipeline::GetGridRank(const int grid) {
    return output_model().GetGridRank(grid);
}

int BmiPipeline::GetGridSize(const int grid) {
    return output_model().GetGridSize(grid);
}

std::string BmiPipeline::GetGridType(const int grid) {
    return output_model().GetGridType(grid);
}

void BmiPipeline::GetGridShape(const int grid, int* shape) {
    output_model().GetGridShape(grid, shape);
}

void BmiPipeline::GetGridSpacing(const int grid, double* spacing) {
    output_model().GetGridSpacing(grid, spacing);
}

void BmiPipeline::GetGridOrigin(const int grid, double* origin) {
    output_model().GetGridOrigin(grid, origin);
}

void BmiPipeline::GetGridX(const int grid, double* x) {
    output_model().GetGridX(grid, x);
}

void BmiPipeline::GetGridY(const int grid, double* y) {
    output_model().GetGridY(grid, y);
}

void BmiPipeline::GetGridZ(const int grid, double* z) {
    output_model().GetGridZ(grid, z);
}

int BmiPipeline::GetGridNodeCount(const int grid) {
    return output_model().GetGridNodeCount(grid);
}

int BmiPipeline::GetGridEdgeCount(const int grid) {
    return output_model().GetGridEdgeCount(grid);
}

int BmiPipeline::GetGridFaceCount(const int grid) {
    return output_model().GetGridFaceCount(grid);
}

void BmiPipeline::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    output_model().GetGridEdgeNodes(grid, edge_nodes);
}

void BmiPipeline::GetGridFaceEdges(const int grid, int* face_edges) {
    output_model().GetGridFaceEdges(grid, face_edges);
}

void BmiPipeline::GetGridFaceNodes(const int grid, int* face_nodes) {
    output_model().GetGridFaceNodes(grid, face_nodes);
}

void BmiPipeline::GetGridNodesPerFace(const int grid, int* nodes_per_face) {
    output_model().GetGridNodesPerFace(grid, nodes_per_face);
}

} // namespace miaow::bmi
