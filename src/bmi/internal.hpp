#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <caf/config_value.hpp>
#include <caf/json.hpp>

#include "miaow_bmi.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

inline constexpr const char* bmi_file_prefix = "bmi_subprocess";

using BmiPipelineConfig = caf::config_value::dictionary;

// Shared data contracts
struct BmiModuleConfig {
    BmiModuleConfig() = default;

    BmiModuleConfig(const caf::json_object& module, std::string_view error_prefix);

    // Module name from the BMI pipeline config
    std::string name_;

    // Dynamic plugin backend for C++ and Fortran BMI modules
    std::optional<DynamicBmiBackend> dynamic_backend_;

    // Dynamic plugin library path
    std::string plugin_path_;

    // BMI initialization config path
    std::string config_file_;

    // C++ BMI factory function name
    std::optional<std::string> cpp_create_function_;

    // C++ BMI destructor function name
    std::optional<std::string> cpp_destroy_function_;

    // Map from target BMI input names to source output names or CSV headers
    std::map<std::string, std::string> variables_names_map_;
};

struct BmiSubprocessTaskSpec {
    // BMI pipeline config argument forwarded through the subprocess boundary
    BmiPipelineConfig pipeline_config_;

    // Dynamic BMI module to execute in the subprocess
    BmiModuleConfig module_;

    // Task output directory written by the subprocess
    std::string output_dir_;

    // Task output basename used for CSV output
    std::string output_name_;
};

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
    explicit ForcingBmi(::bmi::Bmi& clock_model);

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

    // Clock model supplies timestep constraints for the forcing source
    ::bmi::Bmi& clock_model_;

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

    // False for the no-op forcing module used when a pipeline config has no forcing
    bool active_{false};
};

class BmiPipeline : public ::bmi::Bmi {
  public:
    explicit BmiPipeline(std::unique_ptr<::bmi::Bmi> supplied_model = nullptr);

    BmiPipeline(const BmiPipeline&) = delete;
    BmiPipeline& operator=(const BmiPipeline&) = delete;

    ~BmiPipeline() override = default;

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
    [[noreturn]] void throw_config_error(const std::string& message) const;
    caf::json_object read_bmi_multi_params(const caf::json_object& global) const;
    BmiModuleConfig
    parse_single_module_config(const caf::json_object& bmi_multi_params) const;
    std::size_t add_model_module(const BmiModuleConfig& module_config);
    bool add_forcing_module(const caf::json_object& global, std::size_t model_module,
                            std::size_t& forcing_module);
    ::bmi::Bmi& output_model();
    void reset_pipeline();
    void build_couplings(std::size_t source_module, std::size_t target_module,
                         const std::map<std::string, std::string>& variables_names_map);
    void apply_coupling(const BmiCoupling& coupling);

    // Optional model supplied by BmiSimulationUnit<T> for bmi_model modules
    std::unique_ptr<::bmi::Bmi> supplied_model_;

    // Modules constructed from dynamic plugins or internal adapters
    std::vector<std::unique_ptr<::bmi::Bmi>> owned_modules_;

    // Non-owning module update order
    std::vector<::bmi::Bmi*> modules_;

    // Per-module configuration string passed to BMI Initialize
    std::vector<std::string> configs_;

    // Scalar source-to-target transfers applied before target module updates
    std::vector<BmiCoupling> couplings_;

    // Module whose BMI surface is exposed by the pipeline
    std::size_t output_module_{0};

    // Number of modules that completed Initialize and need Finalize
    std::size_t initialized_count_{0};
};

// CAF serialization hooks
template <class Inspector>
bool inspect(Inspector& inspector, BmiSubprocessTaskSpec& subprocess_task) {
    return inspector.object(subprocess_task)
        .fields(inspector.field("pipeline_config", subprocess_task.pipeline_config_),
                inspector.field("backend", subprocess_task.module_.dynamic_backend_),
                inspector.field("plugin_path", subprocess_task.module_.plugin_path_),
                inspector.field("output_dir", subprocess_task.output_dir_),
                inspector.field("output_name", subprocess_task.output_name_),
                inspector.field("cpp_create_function",
                                subprocess_task.module_.cpp_create_function_),
                inspector.field("cpp_destroy_function",
                                subprocess_task.module_.cpp_destroy_function_));
}

// BMI pipeline config and module IO
caf::json_object read_bmi_pipeline_config_file(const std::filesystem::path& path);

caf::config_value::dictionary
make_bmi_multi_global_config(const BmiModuleConfig& module_config,
                             const std::string& forcing_file);

std::filesystem::path bmi_pipeline_config_file_path(const Task& task);

std::vector<std::string> bmi_pipeline_config_artifact_paths(const Task& task);

void write_bmi_pipeline_config_file(const BmiPipelineConfig& pipeline_config,
                                    const std::filesystem::path& path);

// Output CSV writing
OutputFile open_output_file(::bmi::Bmi& model, const std::string& output_dir,
                            const std::string& output_name);

void write_output_step(::bmi::Bmi& model, OutputFile& output_file, double time);

// Subprocess path helpers
inline std::filesystem::path
make_subprocess_result_path(const std::filesystem::path& spec_path) {
    const std::string spec_filename = spec_path.filename().string();
    const std::string spec_suffix = "_spec.json";
    if (spec_filename.size() > spec_suffix.size() &&
        spec_filename.compare(spec_filename.size() - spec_suffix.size(), spec_suffix.size(),
                              spec_suffix) == 0) {
        return spec_path.parent_path() /
               (spec_filename.substr(0, spec_filename.size() - spec_suffix.size()) +
                "_result.json");
    }
    return spec_path.parent_path() / (std::string(bmi_file_prefix) + "_result.json");
}

// Dynamic module construction helpers
inline DynamicBmiBackend require_dynamic_backend(const BmiModuleConfig& module_config,
                                                 const std::string& context) {
    if (!module_config.dynamic_backend_) {
        throw std::runtime_error(context + " requires a dynamic BMI backend");
    }
    return *module_config.dynamic_backend_;
}

inline std::unique_ptr<::bmi::Bmi>
make_dynamic_bmi_model(const BmiModuleConfig& module_config) {
    const DynamicBmiBackend backend =
        require_dynamic_backend(module_config, "BMI module config");
    if (backend == DynamicBmiBackend::cpp) {
        const std::string create_function =
            module_config.cpp_create_function_.value_or("bmi_model_create");
        const std::string destroy_function =
            module_config.cpp_destroy_function_.value_or("bmi_model_destroy");
        return std::make_unique<DynamicCppBmi>(module_config.plugin_path_, create_function,
                                               destroy_function);
    }
    return std::make_unique<DynamicFortranBmi>(module_config.plugin_path_);
}

} // namespace miaow::bmi
