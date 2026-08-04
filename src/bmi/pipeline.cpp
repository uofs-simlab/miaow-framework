#include "miaow_bmi.hpp"
#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace miaow {

using bmi::BmiCoupling;
using bmi::ForcingBmi;
using bmi::bmi_pipeline_config_from_task;
using bmi::read_bmi_pipeline_config_file;

class BmiPipeline::Impl : public bmi_api::Bmi {
  public:
    explicit Impl(std::unique_ptr<::bmi::Bmi> supplied_model = nullptr);
    explicit Impl(const Task& task, std::unique_ptr<::bmi::Bmi> supplied_model = nullptr);

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() override = default;

    void initialize_task();
    void Initialize(std::string config_file) override;
    void Update() override;
    void UpdateUntil(double time) override;
    void Finalize() override;

    std::string GetComponentName() override;
    int GetInputItemCount() override;
    int GetOutputItemCount() override;
    std::vector<std::string> GetInputVarNames() override;
    std::vector<std::string> GetOutputVarNames() override;
    int GetVarGrid(std::string name) override;
    std::string GetVarType(std::string name) override;
    std::string GetVarUnits(std::string name) override;
    int GetVarItemsize(std::string name) override;
    int GetVarNbytes(std::string name) override;
    std::string GetVarLocation(std::string name) override;
    double GetCurrentTime() override;
    double GetStartTime() override;
    double GetEndTime() override;
    std::string GetTimeUnits() override;
    double GetTimeStep() override;
    void GetValue(std::string name, void* dest) override;
    void* GetValuePtr(std::string name) override;
    void GetValueAtIndices(std::string name, void* dest, int* inds, int count) override;
    void SetValue(std::string name, void* src) override;
    void SetValueAtIndices(std::string name, int* inds, int count, void* src) override;
    int GetGridRank(const int grid) override;
    int GetGridSize(const int grid) override;
    std::string GetGridType(const int grid) override;
    void GetGridShape(const int grid, int* shape) override;
    void GetGridSpacing(const int grid, double* spacing) override;
    void GetGridOrigin(const int grid, double* origin) override;
    void GetGridX(const int grid, double* x) override;
    void GetGridY(const int grid, double* y) override;
    void GetGridZ(const int grid, double* z) override;
    int GetGridNodeCount(const int grid) override;
    int GetGridEdgeCount(const int grid) override;
    int GetGridFaceCount(const int grid) override;
    void GetGridEdgeNodes(const int grid, int* edge_nodes) override;
    void GetGridFaceEdges(const int grid, int* face_edges) override;
    void GetGridFaceNodes(const int grid, int* face_nodes) override;
    void GetGridNodesPerFace(const int grid, int* nodes_per_face) override;

    const std::vector<std::string>& output_variables() const;
    const std::optional<double>& output_interval() const;
    double time_step_tolerance() const;
    std::size_t completed_update_count() const;

  private:
    void initialize(const BmiPipelineConfig& pipeline_config);
    std::size_t add_model_module(const BmiModuleConfig& module_config);
    bool add_forcing_module(const BmiPipelineConfig& pipeline_config,
                            std::size_t model_module, std::size_t& forcing_module);
    ::bmi::Bmi& output_model();
    void reset_pipeline();
    void build_couplings(std::size_t source_module, std::size_t target_module,
                         const std::map<std::string, std::string>& variables_names_map);
    void apply_coupling(const BmiCoupling& coupling);

    // Optional supplied model owned by the pipeline
    std::unique_ptr<::bmi::Bmi> supplied_model_;

    // Internal config decoded from a runtime task
    std::optional<BmiPipelineConfig> task_config_;

    // Modules constructed from dynamic plugins or internal adapters
    std::vector<std::unique_ptr<::bmi::Bmi>> owned_modules_;

    // Module pointers ordered by pipeline execution
    std::vector<::bmi::Bmi*> modules_;

    // Per-module configuration string passed to BMI Initialize
    std::vector<std::string> configs_;

    // Scalar source-to-target transfers applied before target module updates
    std::vector<BmiCoupling> couplings_;

    // BMI output variables selected for task CSV output
    std::vector<std::string> output_variables_;

    // Optional model-time interval for runtime-managed output
    std::optional<double> output_interval_;

    // Absolute tolerance for model-time comparisons
    double time_step_tolerance_{default_bmi_time_step_tolerance};

    // Number of complete pipeline updates since initialization
    std::size_t completed_update_count_{0};

    // Module whose BMI surface is exposed by the pipeline
    std::size_t output_module_{0};

    // Number of modules that completed Initialize and need Finalize
    std::size_t initialized_count_{0};
};

// Construct a BMI pipeline from an already supplied model
BmiPipeline::Impl::Impl(std::unique_ptr<::bmi::Bmi> supplied_model)
    : supplied_model_(std::move(supplied_model)) {}

BmiPipeline::Impl::Impl(const Task& task, std::unique_ptr<::bmi::Bmi> supplied_model)
    : supplied_model_(std::move(supplied_model)),
      task_config_(bmi_pipeline_config_from_task(task)) {}

// Add a model module to the pipeline and return its module index
std::size_t BmiPipeline::Impl::add_model_module(const BmiModuleConfig& module_config) {
    if (!module_config.backend_) {
        // supplied_model modules are bound to the model passed by the caller
        if (!supplied_model_) {
            io::throw_config_error("BMI pipeline config error: ",
                                   "module 'supplied_model' requires a supplied BMI model");
        }
        modules_.push_back(supplied_model_.get());
    } else {
        // Dynamic modules are constructed from plugin metadata in the config
        owned_modules_.push_back(std::make_unique<DynamicBmiModel>(module_config));
        modules_.push_back(owned_modules_.back().get());
    }

    configs_.push_back(module_config.config_file_);
    return modules_.size() - 1;
}

// Add the optional forcing module and return whether it is present
bool BmiPipeline::Impl::add_forcing_module(const BmiPipelineConfig& pipeline_config,
                                           std::size_t model_module,
                                           std::size_t& forcing_module) {
    const std::string forcing_file = io::trim(pipeline_config.forcing_file_);
    if (forcing_file.empty()) {
        return false;
    }

    // The forcing module follows every configured model module
    forcing_module = modules_.size();

    // The receiving model clock defines CSV timestep alignment
    owned_modules_.push_back(std::make_unique<ForcingBmi>(*modules_.at(model_module)));
    modules_.push_back(owned_modules_.back().get());
    configs_.push_back(forcing_file);
    return true;
}

// Clear pipeline state to prepare for the next pipeline config initialization
void BmiPipeline::Impl::reset_pipeline() {
    owned_modules_.clear();
    modules_.clear();
    configs_.clear();
    couplings_.clear();
    output_variables_.clear();
    output_interval_.reset();
    time_step_tolerance_ = default_bmi_time_step_tolerance;
    completed_update_count_ = 0;
    output_module_ = 0;
    initialized_count_ = 0;
}

// Build scalar couplings from source outputs to target inputs
void BmiPipeline::Impl::build_couplings(
    std::size_t source_module, std::size_t target_module,
    const std::map<std::string, std::string>& variables_names_map) {
    ::bmi::Bmi& source_model = *modules_.at(source_module);
    ::bmi::Bmi& target_model = *modules_.at(target_module);

    const std::vector<std::string> source_outputs = source_model.GetOutputVarNames();
    const std::vector<std::string> target_inputs = target_model.GetInputVarNames();

    const auto append_validated_coupling = [&](const std::string& source_variable,
                                               const std::string& target_variable) {
        // Variable maps currently apply only to model inputs receiving CSV forcing
        const std::vector<std::string>::const_iterator target_it =
            std::find(target_inputs.begin(), target_inputs.end(), target_variable);
        if (target_it == target_inputs.end()) {
            throw std::runtime_error(
                "BMI forcing error: variables_names_map key '" + target_variable +
                "' is not a BMI input variable; MIAOW maps BMI input variables to CSV "
                "forcing headers");
        }

        // Source variables must be available as BMI outputs
        const std::vector<std::string>::const_iterator source_it =
            std::find(source_outputs.begin(), source_outputs.end(), source_variable);
        if (source_it == source_outputs.end()) {
            throw std::runtime_error("BMI coupling error: source variable '" +
                                     source_variable + "' not found");
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

void BmiPipeline::Impl::initialize_task() {
    if (!task_config_) {
        throw std::runtime_error(
            "BMI pipeline config error: task pipeline config is unavailable");
    }
    initialize(*task_config_);
}

void BmiPipeline::Impl::Initialize(std::string config_file) {
    const BmiPipelineConfig pipeline_config = read_bmi_pipeline_config_file(config_file);
    initialize(pipeline_config);
}

// Initialize pipeline modules from one decoded config
void BmiPipeline::Impl::initialize(const BmiPipelineConfig& pipeline_config) {
    // Reset any previous BMI lifecycle before applying the next pipeline config
    if (initialized_count_ > 0) {
        Finalize();
    }
    reset_pipeline();

    if (pipeline_config.module_configs_.size() != 1) {
        io::throw_config_error("BMI pipeline config error: ",
                               "bmi_multi modules must contain one entry");
    }
    output_module_ = pipeline_config.output_module_;

    // Add the configured model module before attaching optional forcing
    const BmiModuleConfig& module_config = pipeline_config.module_configs_.front();

    // The model module defines whether task CSV output is disabled or selected
    if (module_config.output_variables_) {
        output_variables_ = *module_config.output_variables_;
    }
    output_interval_ = pipeline_config.output_interval_;
    time_step_tolerance_ = pipeline_config.time_step_tolerance_;
    if (!std::isfinite(time_step_tolerance_) || time_step_tolerance_ < 0.0) {
        throw std::runtime_error(
            "BMI pipeline config error: time_step_tolerance must be finite and "
            "non-negative");
    }
    if (output_interval_ &&
        (!std::isfinite(*output_interval_) || *output_interval_ <= 0.0)) {
        throw std::runtime_error(
            "BMI pipeline config error: output_interval must be finite and positive");
    }
    const std::size_t model_module = add_model_module(module_config);

    if (output_module_ >= modules_.size()) {
        throw std::runtime_error("BMI pipeline error: output module is invalid");
    }

    // Add the optional forcing module after the model module so couplings can be built
    std::size_t forcing_module = 0;
    const bool has_forcing =
        add_forcing_module(pipeline_config, model_module, forcing_module);

    // Initialize modules in update order so downstream metadata is available
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index) {
        modules_.at(module_index)->Initialize(configs_.at(module_index));
        ++initialized_count_;
    }

    // An enabled empty selection resolves to every output exposed after initialization
    if (module_config.output_variables_ && module_config.output_variables_->empty()) {
        output_variables_ = GetOutputVarNames();
    }

    // Distinct native updates must remain distinguishable by the configured tolerance
    const double time_step = GetTimeStep();
    if (!std::isfinite(time_step) || time_step <= 0.0) {
        throw std::runtime_error(
            "BMI pipeline error: output model time step must be finite and positive");
    }
    if (time_step_tolerance_ >= time_step / 2.0) {
        throw std::runtime_error(
            "BMI pipeline error: time_step_tolerance must be less than half the output "
            "model time step");
    }

    if (has_forcing) {
        // Couplings are built after initialization so BMI metadata is available
        build_couplings(forcing_module, model_module, module_config.variables_names_map_);
    }
}

// Apply one scalar coupling from a source module output to a target module input
void BmiPipeline::Impl::apply_coupling(const BmiCoupling& coupling) {
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

// Update each module in order, applying couplings before each target module update
void BmiPipeline::Impl::Update() {
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index) {
        for (const BmiCoupling& coupling : couplings_) {
            if (coupling.target_module_ == module_index) {
                apply_coupling(coupling);
            }
        }
        modules_.at(module_index)->Update();
    }
    ++completed_update_count_;
}

// Calls Update() until the output model time is within tolerance of the requested time
void BmiPipeline::Impl::UpdateUntil(double time) {
    double remaining_time = time - GetCurrentTime();
    while (remaining_time > time_step_tolerance_) {
        Update();
        remaining_time = time - GetCurrentTime();
    }

    // Reject an update that overshoots the target beyond the configured tolerance
    if (remaining_time < -time_step_tolerance_) {
        throw std::runtime_error(
            "BMI pipeline error: update passed the requested target time");
    }
}

void BmiPipeline::Impl::Finalize() {
    // Finalize every module before rethrowing the first finalization error
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

    // Throw the first exception encountered during Finalize if one occurred; otherwise,
    // assume all models in the pipeline finalized successfully
    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

// The pipeline exposes one selected module as its BMI surface
::bmi::Bmi& BmiPipeline::Impl::output_model() {
    return *modules_.at(output_module_);
}

std::string BmiPipeline::Impl::GetComponentName() {
    return output_model().GetComponentName();
}

int BmiPipeline::Impl::GetInputItemCount() {
    return output_model().GetInputItemCount();
}

int BmiPipeline::Impl::GetOutputItemCount() {
    return output_model().GetOutputItemCount();
}

std::vector<std::string> BmiPipeline::Impl::GetInputVarNames() {
    return output_model().GetInputVarNames();
}

std::vector<std::string> BmiPipeline::Impl::GetOutputVarNames() {
    return output_model().GetOutputVarNames();
}

int BmiPipeline::Impl::GetVarGrid(std::string name) {
    return output_model().GetVarGrid(name);
}

std::string BmiPipeline::Impl::GetVarType(std::string name) {
    return output_model().GetVarType(name);
}

std::string BmiPipeline::Impl::GetVarUnits(std::string name) {
    return output_model().GetVarUnits(name);
}

int BmiPipeline::Impl::GetVarItemsize(std::string name) {
    return output_model().GetVarItemsize(name);
}

int BmiPipeline::Impl::GetVarNbytes(std::string name) {
    return output_model().GetVarNbytes(name);
}

std::string BmiPipeline::Impl::GetVarLocation(std::string name) {
    return output_model().GetVarLocation(name);
}

double BmiPipeline::Impl::GetCurrentTime() {
    return output_model().GetCurrentTime();
}

double BmiPipeline::Impl::GetStartTime() {
    return output_model().GetStartTime();
}

double BmiPipeline::Impl::GetEndTime() {
    return output_model().GetEndTime();
}

std::string BmiPipeline::Impl::GetTimeUnits() {
    return output_model().GetTimeUnits();
}

double BmiPipeline::Impl::GetTimeStep() {
    return output_model().GetTimeStep();
}

void BmiPipeline::Impl::GetValue(std::string name, void* dest) {
    output_model().GetValue(name, dest);
}

void* BmiPipeline::Impl::GetValuePtr(std::string name) {
    return output_model().GetValuePtr(name);
}

void BmiPipeline::Impl::GetValueAtIndices(std::string name, void* dest, int* inds,
                                          int count) {
    output_model().GetValueAtIndices(name, dest, inds, count);
}

void BmiPipeline::Impl::SetValue(std::string name, void* src) {
    output_model().SetValue(name, src);
}

void BmiPipeline::Impl::SetValueAtIndices(std::string name, int* inds, int count,
                                          void* src) {
    output_model().SetValueAtIndices(name, inds, count, src);
}

int BmiPipeline::Impl::GetGridRank(const int grid) {
    return output_model().GetGridRank(grid);
}

int BmiPipeline::Impl::GetGridSize(const int grid) {
    return output_model().GetGridSize(grid);
}

std::string BmiPipeline::Impl::GetGridType(const int grid) {
    return output_model().GetGridType(grid);
}

void BmiPipeline::Impl::GetGridShape(const int grid, int* shape) {
    output_model().GetGridShape(grid, shape);
}

void BmiPipeline::Impl::GetGridSpacing(const int grid, double* spacing) {
    output_model().GetGridSpacing(grid, spacing);
}

void BmiPipeline::Impl::GetGridOrigin(const int grid, double* origin) {
    output_model().GetGridOrigin(grid, origin);
}

void BmiPipeline::Impl::GetGridX(const int grid, double* x) {
    output_model().GetGridX(grid, x);
}

void BmiPipeline::Impl::GetGridY(const int grid, double* y) {
    output_model().GetGridY(grid, y);
}

void BmiPipeline::Impl::GetGridZ(const int grid, double* z) {
    output_model().GetGridZ(grid, z);
}

int BmiPipeline::Impl::GetGridNodeCount(const int grid) {
    return output_model().GetGridNodeCount(grid);
}

int BmiPipeline::Impl::GetGridEdgeCount(const int grid) {
    return output_model().GetGridEdgeCount(grid);
}

int BmiPipeline::Impl::GetGridFaceCount(const int grid) {
    return output_model().GetGridFaceCount(grid);
}

void BmiPipeline::Impl::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    output_model().GetGridEdgeNodes(grid, edge_nodes);
}

void BmiPipeline::Impl::GetGridFaceEdges(const int grid, int* face_edges) {
    output_model().GetGridFaceEdges(grid, face_edges);
}

void BmiPipeline::Impl::GetGridFaceNodes(const int grid, int* face_nodes) {
    output_model().GetGridFaceNodes(grid, face_nodes);
}

void BmiPipeline::Impl::GetGridNodesPerFace(const int grid, int* nodes_per_face) {
    output_model().GetGridNodesPerFace(grid, nodes_per_face);
}

// Expose selected CSV variables to lifecycle output writing
const std::vector<std::string>& BmiPipeline::Impl::output_variables() const {
    return output_variables_;
}

const std::optional<double>& BmiPipeline::Impl::output_interval() const {
    return output_interval_;
}

double BmiPipeline::Impl::time_step_tolerance() const {
    return time_step_tolerance_;
}

std::size_t BmiPipeline::Impl::completed_update_count() const {
    return completed_update_count_;
}

BmiPipeline::BmiPipeline(std::unique_ptr<bmi_api::Bmi> model)
    : impl_(std::make_unique<Impl>(std::move(model))) {}

BmiPipeline::BmiPipeline(const Task& task, std::unique_ptr<bmi_api::Bmi> model)
    : impl_(std::make_unique<Impl>(task, std::move(model))) {}

BmiPipeline::~BmiPipeline() = default;

void BmiPipeline::initialize_task() {
    impl_->initialize_task();
}

void BmiPipeline::Initialize(std::string config_file) {
    impl_->Initialize(std::move(config_file));
}

void BmiPipeline::Update() {
    impl_->Update();
}

void BmiPipeline::UpdateUntil(double time) {
    impl_->UpdateUntil(time);
}

void BmiPipeline::Finalize() {
    impl_->Finalize();
}

std::string BmiPipeline::GetComponentName() {
    return impl_->GetComponentName();
}

int BmiPipeline::GetInputItemCount() {
    return impl_->GetInputItemCount();
}

int BmiPipeline::GetOutputItemCount() {
    return impl_->GetOutputItemCount();
}

std::vector<std::string> BmiPipeline::GetInputVarNames() {
    return impl_->GetInputVarNames();
}

std::vector<std::string> BmiPipeline::GetOutputVarNames() {
    return impl_->GetOutputVarNames();
}

int BmiPipeline::GetVarGrid(std::string name) {
    return impl_->GetVarGrid(std::move(name));
}

std::string BmiPipeline::GetVarType(std::string name) {
    return impl_->GetVarType(std::move(name));
}

std::string BmiPipeline::GetVarUnits(std::string name) {
    return impl_->GetVarUnits(std::move(name));
}

int BmiPipeline::GetVarItemsize(std::string name) {
    return impl_->GetVarItemsize(std::move(name));
}

int BmiPipeline::GetVarNbytes(std::string name) {
    return impl_->GetVarNbytes(std::move(name));
}

std::string BmiPipeline::GetVarLocation(std::string name) {
    return impl_->GetVarLocation(std::move(name));
}

double BmiPipeline::GetCurrentTime() {
    return impl_->GetCurrentTime();
}

double BmiPipeline::GetStartTime() {
    return impl_->GetStartTime();
}

double BmiPipeline::GetEndTime() {
    return impl_->GetEndTime();
}

std::string BmiPipeline::GetTimeUnits() {
    return impl_->GetTimeUnits();
}

double BmiPipeline::GetTimeStep() {
    return impl_->GetTimeStep();
}

void BmiPipeline::GetValue(std::string name, void* dest) {
    impl_->GetValue(std::move(name), dest);
}

void* BmiPipeline::GetValuePtr(std::string name) {
    return impl_->GetValuePtr(std::move(name));
}

void BmiPipeline::GetValueAtIndices(std::string name, void* dest, int* inds, int count) {
    impl_->GetValueAtIndices(std::move(name), dest, inds, count);
}

void BmiPipeline::SetValue(std::string name, void* src) {
    impl_->SetValue(std::move(name), src);
}

void BmiPipeline::SetValueAtIndices(std::string name, int* inds, int count, void* src) {
    impl_->SetValueAtIndices(std::move(name), inds, count, src);
}

int BmiPipeline::GetGridRank(const int grid) {
    return impl_->GetGridRank(grid);
}

int BmiPipeline::GetGridSize(const int grid) {
    return impl_->GetGridSize(grid);
}

std::string BmiPipeline::GetGridType(const int grid) {
    return impl_->GetGridType(grid);
}

void BmiPipeline::GetGridShape(const int grid, int* shape) {
    impl_->GetGridShape(grid, shape);
}

void BmiPipeline::GetGridSpacing(const int grid, double* spacing) {
    impl_->GetGridSpacing(grid, spacing);
}

void BmiPipeline::GetGridOrigin(const int grid, double* origin) {
    impl_->GetGridOrigin(grid, origin);
}

void BmiPipeline::GetGridX(const int grid, double* x) {
    impl_->GetGridX(grid, x);
}

void BmiPipeline::GetGridY(const int grid, double* y) {
    impl_->GetGridY(grid, y);
}

void BmiPipeline::GetGridZ(const int grid, double* z) {
    impl_->GetGridZ(grid, z);
}

int BmiPipeline::GetGridNodeCount(const int grid) {
    return impl_->GetGridNodeCount(grid);
}

int BmiPipeline::GetGridEdgeCount(const int grid) {
    return impl_->GetGridEdgeCount(grid);
}

int BmiPipeline::GetGridFaceCount(const int grid) {
    return impl_->GetGridFaceCount(grid);
}

void BmiPipeline::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    impl_->GetGridEdgeNodes(grid, edge_nodes);
}

void BmiPipeline::GetGridFaceEdges(const int grid, int* face_edges) {
    impl_->GetGridFaceEdges(grid, face_edges);
}

void BmiPipeline::GetGridFaceNodes(const int grid, int* face_nodes) {
    impl_->GetGridFaceNodes(grid, face_nodes);
}

void BmiPipeline::GetGridNodesPerFace(const int grid, int* nodes_per_face) {
    impl_->GetGridNodesPerFace(grid, nodes_per_face);
}

// Forward selected CSV variables from the implementation
const std::vector<std::string>& BmiPipeline::output_variables() const {
    return impl_->output_variables();
}

const std::optional<double>& BmiPipeline::output_interval() const {
    return impl_->output_interval();
}

double BmiPipeline::time_step_tolerance() const {
    return impl_->time_step_tolerance();
}

std::size_t BmiPipeline::completed_update_count() const {
    return impl_->completed_update_count();
}

} // namespace miaow
