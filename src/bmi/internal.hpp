#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>
#include <caf/config_value.hpp>

#include "miaow_bmi.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// Internal model module representation used by pipeline serialization
struct BmiModuleConfig {
    BmiModuleConfig(std::string name, std::string config_file,
                    std::map<std::string, std::string> variables_names_map = {});

    BmiModuleConfig(std::string name, DynamicBmiBackend backend,
                    std::string plugin_path, std::string config_file = "",
                    std::map<std::string, std::string> variables_names_map = {});

    BmiModuleConfig(const boost::json::object& module_object,
                    std::string_view error_prefix);

    std::string name_;
    std::optional<DynamicBmiBackend> backend_;
    std::string plugin_path_;
    std::string config_file_;
    std::optional<std::string> cpp_create_function_;
    std::optional<std::string> cpp_destroy_function_;
    std::map<std::string, std::string> variables_names_map_;
    std::optional<std::vector<std::string>> output_variables_;
};

// Internal task payload consumed by BmiPipeline; created from a user-supplied or
// realization-created StaticBmiConfig or DynamicBmiConfig
struct BmiPipelineConfig {
    BmiPipelineConfig(std::vector<BmiModuleConfig> module_configs,
                      std::string forcing_file = "", std::string feature_id = "",
                      std::optional<double> output_interval = std::nullopt,
                      double time_step_tolerance_seconds =
                          default_bmi_time_step_tolerance_seconds);

    BmiPipelineConfig(const boost::json::object& pipeline_config,
                      std::string_view error_prefix);

    explicit BmiPipelineConfig(const Task& task);

    std::vector<BmiModuleConfig> module_configs_;
    std::size_t output_module_{0};
    std::string forcing_file_;
    std::string feature_id_;
    std::optional<double> output_interval_;
    double time_step_tolerance_seconds_{default_bmi_time_step_tolerance_seconds};
};

// Convert an internal pipeline config into the generic Task payload
caf::config_value bmi_pipeline_config_to_value(const BmiPipelineConfig& pipeline_config);

// Internal adapter for one dynamically loaded C++ or Fortran BMI plugin
class DynamicBmiModel : public bmi_api::Bmi {
  public:
    explicit DynamicBmiModel(const BmiModuleConfig& module_config);

    DynamicBmiModel(const DynamicBmiModel&) = delete;
    DynamicBmiModel& operator=(const DynamicBmiModel&) = delete;

    ~DynamicBmiModel() override;

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

  private:
    using CreateBmiFunction = bmi_api::Bmi* (*)();
    using DestroyBmiFunction = void (*)(bmi_api::Bmi*);
    using RegisterBmiFunction = int (*)(void**);
    using UnregisterBmiFunction = int (*)(void**);

    void open_cpp_plugin(const BmiModuleConfig& module_config);
    void open_fortran_plugin(const BmiModuleConfig& module_config);
    void finalize_fortran_model();
    void unregister_fortran_model();
    void cleanup_model();

    DynamicBmiBackend backend_;
    void* library_handle_{nullptr};
    bmi_api::Bmi* cpp_model_{nullptr};
    DestroyBmiFunction destroy_bmi_{nullptr};
    void* fortran_model_handle_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
    UnregisterBmiFunction unregister_bmi_{nullptr};
    bool initialized_{false};
};

} // namespace miaow

namespace miaow::bmi {

// Convert one supported fixed-duration BMI time unit to seconds
double seconds_per_time_unit(std::string_view raw_units,
                             std::string_view error_prefix);

// Validate one forcing interval against the receiving model clock
void validate_forcing_time_step_delta(double actual_seconds, double expected_seconds,
                                      double tolerance_seconds,
                                      std::size_t record_number);

// Normalized scalar types used by cached BMI metadata
enum class BmiValueType {
    // BMI "float" represented by C++ float
    bmi_float,

    // BMI "double" represented by C++ double
    bmi_double,

    // BMI "int" represented by C++ int
    bmi_int,
};

// Cached metadata for one initialized BMI variable
class BmiVariable {
  public:
    // Construct from known metadata such as forcing reader metadata
    BmiVariable(std::string name, BmiValueType type, std::string units,
                std::string location, int grid_id, std::size_t item_count);

    // Query and validate metadata from an initialized BMI model
    BmiVariable(::bmi::Bmi& model, const std::string& name,
                std::string_view error_prefix);

    // Expose immutable variable metadata
    const std::string& name() const;
    BmiValueType type() const;
    const std::string& units() const;
    const std::string& location() const;
    int grid_id() const;
    std::size_t item_count() const;

  private:
    // BMI variable name
    std::string name_;

    // Normalized scalar value type
    BmiValueType type_{BmiValueType::bmi_double};

    // Physical units reported by the model
    std::string units_;

    // Variable placement on the referenced grid
    std::string location_;

    // Model-local BMI grid identifier
    int grid_id_{0};

    // Number of flattened scalar values
    std::size_t item_count_{0};
};

std::string_view bmi_value_type_name(BmiValueType type);

std::size_t bmi_value_type_size(BmiValueType type);

// Format-neutral task output sink
class OutputWriter {
  public:
    virtual ~OutputWriter() = default;

    const std::string& artifact_path() const;

    virtual void write_step(::bmi::Bmi& model, double time) = 0;

  protected:
    explicit OutputWriter(std::string artifact_path);

  private:
    // Artifact path relative to the job directory
    std::string artifact_path_;
};

// CSV task output implementation
class CsvOutputWriter final : public OutputWriter {
  public:
    CsvOutputWriter(::bmi::Bmi& model, const std::string& output_dir,
                    const std::string& output_name,
                    const std::vector<std::string>& output_variables);

    void write_step(::bmi::Bmi& model, double time) override;

  private:
    // Resolved BMI variables written to this output
    std::vector<BmiVariable> variables_;

    // CSV output stream for this task
    std::ofstream stream_;
};

// Format-neutral forcing source (currently only CSV is supported)
class ForcingReader {
  public:
    virtual ~ForcingReader() = default;

    const std::vector<BmiVariable>& variables() const;

    virtual double time_seconds() const = 0;
    virtual bool advance() = 0;
    virtual void copy_value(std::size_t variable_index, void* destination) const = 0;

  protected:
    void initialize_variables(std::vector<BmiVariable> variables);

  private:
    // Variables exposed in the value order fixed during reader construction
    std::vector<BmiVariable> variables_;
};

// CSV forcing source implementation
class CsvForcingReader final : public ForcingReader {
  public:
    explicit CsvForcingReader(const std::filesystem::path& forcing_path);

    double time_seconds() const override;
    bool advance() override;
    void copy_value(std::size_t variable_index, void* destination) const override;

  private:
    // Streaming CSV source positioned after the last parsed line
    std::ifstream stream_;

    // Zero-based time column source index
    std::size_t time_column_index_{0};

    // Source column indices ordered according to variables()
    std::vector<std::size_t> source_column_indices_;

    // Current source record
    std::int64_t current_timestamp_seconds_{0};
    std::vector<double> current_values_;

    // Last physical CSV line read from the stream
    std::size_t last_line_number_{1};
};

// Resolved pipeline coupling endpoints
class BmiCoupling {
  public:
    // Query and validate both initialized endpoints
    BmiCoupling(std::size_t source_module_index, ::bmi::Bmi& source_model,
                const std::string& source_name, std::size_t target_module_index,
                ::bmi::Bmi& target_model, const std::string& target_name);

    // Expose module positions used by pipeline orchestration
    std::size_t source_module_index() const;
    std::size_t target_module_index() const;

    // Transfer the complete source variable to the target variable
    void apply(::bmi::Bmi& source_model, ::bmi::Bmi& target_model) const;

  private:
    // Zero-based index of the source module in the pipeline module sequence
    std::size_t source_module_index_;

    // Resolved BMI output variable
    BmiVariable source_variable_;

    // Zero-based index of the target module in the pipeline module sequence
    std::size_t target_module_index_;

    // Resolved BMI input variable
    BmiVariable target_variable_;
};

// Prototype orchestration core for sequential multi-model BMI pipelines
//
// The current pipeline accepts one supplied or dynamically loaded model and an optional
// ForcingBmi provider. It owns one Task lifecycle, stores modules and couplings in
// execution order, and reports task output artifacts.
class BmiPipeline {
  public:
    // Construct one pipeline execution with an optional statically supplied BMI model
    explicit BmiPipeline(std::unique_ptr<::bmi::Bmi> supplied_model = nullptr);

    BmiPipeline(const BmiPipeline&) = delete;
    BmiPipeline& operator=(const BmiPipeline&) = delete;

    // Execute the standard BMI lifecycle and return task artifacts
    TaskResult execute_bmi_lifecycle(const Task& task);

  private:
    void initialize(const BmiPipelineConfig& pipeline_config);
    std::size_t add_model_module(const BmiModuleConfig& module_config);
    bool add_forcing_module(const std::string& forcing_file,
                            double time_step_tolerance_seconds,
                            std::size_t model_module_index,
                            std::size_t& forcing_module_index);
    void build_couplings(std::size_t source_module_index, std::size_t target_module_index,
                         const std::map<std::string, std::string>& variables_names_map);
    void update();
    void update_until(double time);
    void finalize();
    ::bmi::Bmi& output_model();

    // Owned modules ordered by pipeline execution
    std::vector<std::unique_ptr<::bmi::Bmi>> modules_;

    // Source-to-target variable transfers applied before target module updates
    std::vector<BmiCoupling> couplings_;

    // BMI output variables selected for task CSV output
    std::vector<std::string> output_variables_;

    // Optional model-time interval for runtime-managed output
    std::optional<double> output_interval_;

    // Absolute tolerance in the time unit exposed by the output model
    double time_step_tolerance_{0.0};

    // Number of complete pipeline updates since initialization
    std::size_t completed_update_count_{0};

    // Module selected for task output
    std::size_t output_module_{0};

    // Initialized modules that still need Finalize
    std::size_t initialized_module_count_{0};
};

// Internal BMI model adapters
class ForcingBmi : public ::bmi::Bmi {
  public:
    ForcingBmi(::bmi::Bmi& target_model, double time_step_tolerance_seconds);

    ForcingBmi(const ForcingBmi&) = delete;
    ForcingBmi& operator=(const ForcingBmi&) = delete;

    ~ForcingBmi() override = default;

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

  private:
    std::size_t find_output_index(const std::string& name) const;
    void check_grid_id(int grid) const;

    // Receiving model supplies timestep constraints for the forcing stream
    ::bmi::Bmi& target_model_;

    // Format-specific forcing source active for this BMI lifecycle
    std::unique_ptr<ForcingReader> reader_;

    // Current timestep index returned by GetValue
    std::size_t step_{0};

    // Current forcing time exposed through the BMI clock methods
    double current_time_seconds_{0.0};

    // Expected forcing timestep from the target model BMI clock
    double time_step_seconds_{0.0};

    // Absolute tolerance for forcing timestep comparisons in seconds
    double time_step_tolerance_seconds_{default_bmi_time_step_tolerance_seconds};

    // True after Update reaches the end of the forcing source
    bool exhausted_{true};
};

std::optional<std::string> write_bmi_pipeline_config_artifact(const Task& task);

caf::config_value::dictionary
make_bmi_multi_global_config(const std::vector<BmiModuleConfig>& module_configs,
                             const std::string& forcing_file,
                             std::size_t output_module = 0,
                             std::optional<double> output_interval = std::nullopt,
                             double time_step_tolerance_seconds =
                                 default_bmi_time_step_tolerance_seconds);

Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const StaticBmiConfig& config);

Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const DynamicBmiConfig& config,
                   const std::string& feature_id = "",
                   TaskMetadata metadata = {});

} // namespace miaow::bmi
