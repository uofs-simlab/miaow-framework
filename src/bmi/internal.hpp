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

    std::string name_;
    std::optional<DynamicBmiBackend> backend_;
    std::string plugin_path_;
    std::string config_file_;
    std::optional<std::string> cpp_create_function_;
    std::optional<std::string> cpp_destroy_function_;
    std::map<std::string, std::string> variables_names_map_;
    std::vector<std::string> output_variables_;
};

// Internal task payload consumed by BmiPipeline
struct BmiPipelineConfig {
    BmiPipelineConfig(std::vector<BmiModuleConfig> module_configs,
                      std::string forcing_file = "", std::string feature_id = "",
                      std::size_t output_interval_steps = 1);

    std::vector<BmiModuleConfig> module_configs_;
    std::size_t output_module_{0};
    std::string forcing_file_;
    std::string feature_id_;
    std::size_t output_interval_steps_{1};
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
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaow

namespace miaow::bmi {

struct OutputVariable {
    // BMI output variable name
    std::string name_;

    // Number of scalar items in the variable value
    std::size_t n_items_;
};

struct OutputFile {
    // Artifact path relative to the job directory
    std::string artifact_path_;

    // BMI output variables written as columns
    std::vector<OutputVariable> variables_;

    // CSV output stream for this task
    std::ofstream stream_;
};

struct ForcingColumn {
    // BMI output name and source CSV header
    std::string csv_header_;

    // Zero-based CSV column index
    std::size_t csv_column_index_;
};

struct ForcingRow {
    // Parsed timestamp from the time column
    std::int64_t timestamp_seconds_{0};

    // Physical one-based line number
    std::size_t row_number_{0};

    // Values ordered like forcing_columns_
    std::vector<double> values_;
};

struct BmiCoupling {
    // Index of the module that provides the scalar value
    std::size_t source_module_;

    // BMI variable read from the source module
    std::string source_variable_;

    // Index of the module that receives the scalar value
    std::size_t target_module_;

    // BMI variable set on the target module
    std::string target_variable_;
};

// Internal BMI model adapters
class ForcingBmi : public ::bmi::Bmi {
  public:
    explicit ForcingBmi(::bmi::Bmi& target_model);

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
    void open_stream(const std::filesystem::path& forcing_path);
    bool read_next_row(ForcingRow& row);
    std::size_t find_output_index(const std::string& name) const;
    void check_grid_id(int grid) const;

    // Receiving model supplies timestep constraints for the forcing stream
    ::bmi::Bmi& target_model_;

    // Streaming CSV source positioned after the current forcing row
    std::ifstream csv_stream_;

    // Time column metadata from the CSV header
    std::string time_column_name_;
    std::size_t csv_time_column_index_{0};

    // Forcing CSV columns exposed as BMI outputs in this order
    std::vector<ForcingColumn> forcing_columns_;

    // Current row exposed through GetValue until Update reads the next row
    ForcingRow current_row_;

    // Last physical CSV row number read from the stream
    std::size_t last_csv_row_number_{1};

    // Current timestep index returned by GetValue
    std::size_t step_{0};

    // Current forcing time exposed through the BMI clock methods
    double current_time_seconds_{0.0};

    // Expected forcing timestep from the target model BMI clock
    double time_step_seconds_{0.0};

    // True after Update reaches the end of the forcing CSV
    bool exhausted_{true};

    // True while forcing data is initialized
    bool active_{false};
};

// BMI pipeline config and module IO
BmiPipelineConfig read_bmi_pipeline_config_file(const std::filesystem::path& path);

void write_bmi_pipeline_config_file(const Task& task, const std::filesystem::path& path);

caf::config_value::dictionary
make_bmi_multi_global_config(const std::vector<BmiModuleConfig>& module_configs,
                             const std::string& forcing_file,
                             std::size_t output_module = 0,
                             std::size_t output_interval_steps = 1);

BmiModuleConfig parse_bmi_module_config(const boost::json::object& module_object,
                                        std::string_view error_prefix);

Task make_dynamic_bmi_task(SimulationUnitId simulation_unit_id,
                           const DynamicBmiConfig& config,
                           const std::string& feature_id = "",
                           TaskMetadata metadata = {});

std::filesystem::path bmi_pipeline_config_file_path(const Task& task);

std::vector<std::string> bmi_pipeline_config_artifact_paths(const Task& task);

// Output CSV writing for an explicit non-empty variable selection
OutputFile open_output_file(::bmi::Bmi& model, const std::string& output_dir,
                            const std::string& output_name,
                            const std::vector<std::string>& output_variables);

void write_output_step(::bmi::Bmi& model, OutputFile& output_file, double time);

} // namespace miaow::bmi
