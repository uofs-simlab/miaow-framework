#pragma once

#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// Avoid confusion with the `miaow::bmi` namespace
namespace bmi_api = ::bmi;

/**
 * @brief Backend interface exposed by a dynamically loaded BMI plugin.
 *
 * Dynamic modules adapt plugins that implement the
 * [Basic Model Interface](https://bmi.csdms.io/en/stable/).
 */
enum class DynamicBmiBackend {
    cpp,
    fortran,
};

/**
 * @brief Model module configuration for `BmiPipelineConfig` and `DynamicBmiModel`.
 *
 * Dynamic module entries mirror the model-module parts of NextGen-style
 * realization files used by [ngen](https://github.com/NOAA-OWP/ngen).
 */
struct BmiModuleConfig {
    /**
     * @brief Construct a supplied-model module config.
     * @param name Module name written to the serialized pipeline config.
     * @param config_file BMI initialization config path.
     * @param variables_names_map Optional BMI input to source variable map.
     */
    BmiModuleConfig(std::string name, std::string config_file,
                    std::map<std::string, std::string> variables_names_map = {});

    /**
     * @brief Construct a dynamic plugin module config.
     * @param name Module name written to the serialized pipeline config.
     * @param dynamic_backend Dynamic plugin backend.
     * @param plugin_path Dynamic plugin library path.
     * @param config_file Optional BMI initialization config path.
     * @param variables_names_map Optional BMI input to source variable map.
     */
    BmiModuleConfig(std::string name, DynamicBmiBackend dynamic_backend,
                    std::string plugin_path, std::string config_file = "",
                    std::map<std::string, std::string> variables_names_map = {});

    // Module name written to the serialized pipeline config
    std::string name_;

    // Dynamic plugin backend for C++ and Fortran BMI modules
    std::optional<DynamicBmiBackend> dynamic_backend_;

    // Dynamic plugin library path
    std::string plugin_path_;

    // BMI initialization config path written into serialized modules
    std::string config_file_;

    // C++ BMI factory function name
    std::optional<std::string> cpp_create_function_;

    // C++ BMI destructor function name
    std::optional<std::string> cpp_destroy_function_;

    // Map from target BMI input names to source output names or CSV headers
    std::map<std::string, std::string> variables_names_map_;

    // BMI output variables written to task CSV output in declaration order
    std::vector<std::string> output_variables_;
};

/**
 * @brief Typed task-level BMI pipeline configuration.
 *
 * `BmiPipelineConfig` stores the model modules, forcing source, and optional
 * feature identifier for one BMI task. Convert it with
 * `bmi_pipeline_config_to_value(...)` before placing it in `Task::args_`.
 */
struct BmiPipelineConfig {
    /**
     * @brief Construct a pipeline config from model modules and optional inputs.
     * @param module_configs Model modules included in the pipeline.
     * @param forcing_file CSV forcing file path. Empty string disables task forcing.
     * @param feature_id Optional feature identifier for realization-derived tasks.
     * @throws std::runtime_error if the module list is empty.
     */
    BmiPipelineConfig(std::vector<BmiModuleConfig> module_configs,
                      std::string forcing_file = "", std::string feature_id = "");

    // Model modules configured for this pipeline
    std::vector<BmiModuleConfig> module_configs_;

    // Zero-based module index exposed through the pipeline BMI surface
    std::size_t output_module_{0};

    // CSV forcing file consumed by the internal forcing module when non-empty
    std::string forcing_file_;

    // Optional feature identifier for realization-derived tasks
    std::string feature_id_;
};

/**
 * @brief Build a `BmiPipelineConfig` for one BMI model.
 *
 * The generated `BmiPipelineConfig` is used with
 * `BmiSimulationUnit<BmiModelType>` for a single model linked into the
 * executable and supplied directly to the pipeline.
 *
 * @param init_config BMI initialization config file path.
 * @param forcing_file CSV forcing file path. Empty string disables task forcing.
 * @param variables_names_map Optional BMI input to forcing CSV header map.
 * @return Top-level `BmiPipelineConfig`.
 * @throws std::runtime_error if required path or mapping entries are empty.
 */
BmiPipelineConfig
make_bmi_model_config(const std::string& init_config, const std::string& forcing_file = "",
                      const std::map<std::string, std::string>& variables_names_map = {});

/**
 * @brief Convert a `BmiPipelineConfig` to a `Task` argument value.
 * @param pipeline_config Typed pipeline config.
 * @return CAF config value suitable for `Task::args_`.
 * @throws std::runtime_error if the pipeline config is invalid.
 */
caf::config_value bmi_pipeline_config_to_value(const BmiPipelineConfig& pipeline_config);

/**
 * @brief Build a `Task` for one dynamically loaded BMI plugin module.
 *
 * The task argument stores a serialized single-module `BmiPipelineConfig` for
 * use with `BmiSimulationUnit<BmiPipeline>`.
 *
 * @param simulation_unit_id `SimulationUnitId` for a registered BMI pipeline unit.
 * @param module_name Module name written to the serialized pipeline config.
 * @param dynamic_backend Dynamic plugin backend.
 * @param plugin_path Dynamic plugin library path.
 * @param config_file Optional BMI initialization config path.
 * @param output_variables BMI output variables written to task CSV output.
 * @param execution_mode Execution mode selected for the returned task.
 * @return Task configured for the dynamic BMI pipeline.
 * @throws std::runtime_error if the generated pipeline config is invalid.
 */
Task make_dynamic_bmi_task(
    SimulationUnitId simulation_unit_id, const std::string& module_name,
    DynamicBmiBackend dynamic_backend, const std::string& plugin_path,
    const std::string& config_file,
    const std::vector<std::string>& output_variables = {},
    ExecutionMode execution_mode = ExecutionMode::in_process);

/**
 * @brief BMI model backed by a single dynamically loaded C++ or Fortran BMI plugin.
 *
 * `DynamicBmiModel` is one model module. It does not own task-level forcing,
 * coupling, output writing, artifact tracking, or execution. Dynamic plugin
 * tasks usually register `BmiSimulationUnit<BmiPipeline>` and let the pipeline
 * construct `DynamicBmiModel` modules from `BmiModuleConfig` entries.
 *
 * The wrapper exposes the
 * [CSDMS BMI C++ interface](https://github.com/csdms/bmi-cxx) for the loaded
 * plugin. The `BmiModuleConfig` selects the dynamic backend and plugin library
 * path. C++ plugins must export `bmi_model_create()` and
 * `bmi_model_destroy(...)` unless the module config names different symbols.
 * Fortran plugins must export `register_bmi(void**)` for the linked
 * [NOAA-OWP procedural BMI bridge](https://github.com/NOAA-OWP/iso_c_fortran_bmi).
 * If a Fortran plugin exports `unregister_bmi(void**)`, MIAOW calls it after
 * BMI finalization.
 */
class DynamicBmiModel : public bmi_api::Bmi {
  public:
    /**
     * @brief Open a dynamic BMI plugin described by a module config.
     * @param module_config Module config with backend and plugin path.
     * @throws std::runtime_error if the plugin path or required symbols are
     * invalid, or if a C++ model creation function returns null.
     */
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

/**
 * @brief Complete task-level BMI pipeline backed by a `BmiPipelineConfig`.
 *
 * `BmiPipeline` owns the
 * [BMI lifecycle](https://bmi.csdms.io/en/stable/) for one task. It reads a
 * `BmiPipelineConfig`, constructs configured model modules or receives one
 * model from `BmiSimulationUnit`, connects forcing to model inputs, writes
 * output, and reports artifacts through the task result.
 *
 * Use `BmiSimulationUnit<BmiPipeline>` for realization-derived tasks, dynamic
 * plugin modules, or other configs that describe their own model modules. Use
 * `BmiSimulationUnit<YourModel>` when a linked model instance must be supplied
 * directly to the pipeline.
 */
class BmiPipeline : public bmi_api::Bmi {
  public:
    /**
     * @brief Construct an empty pipeline that can be initialized from a config path.
     * @param model Optional single model provided by `BmiSimulationUnit`.
     */
    explicit BmiPipeline(std::unique_ptr<bmi_api::Bmi> model = nullptr);

    /**
     * @brief Construct a task pipeline and write its `BmiPipelineConfig` file.
     * @param task Task whose first argument stores a serialized `BmiPipelineConfig`.
     * @param model Optional single model provided by `BmiSimulationUnit`.
     */
    explicit BmiPipeline(const Task& task, std::unique_ptr<bmi_api::Bmi> model = nullptr);

    BmiPipeline(const BmiPipeline&) = delete;
    BmiPipeline& operator=(const BmiPipeline&) = delete;

    ~BmiPipeline() override;

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

    std::string config_path_string() const;
    std::vector<std::string> artifact_paths() const;

    /**
     * @brief Execute this pipeline through the standard BMI lifecycle.
     * @param task Task whose output settings receive BMI artifacts.
     * @return Structured `TaskResult` for pipeline lifecycle execution.
     */
    TaskResult execute_bmi_lifecycle(const Task& task);

  private:
    const std::vector<std::string>& output_variables() const;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief BMI realization partitioned into one `BmiPipelineConfig` per feature.
 *
 * The constructor reads a
 * [NextGen-style](https://github.com/NOAA-OWP/ngen) global realization with
 * one `bmi_fortran` module and `CsvPerFeature` forcing, resolves relative
 * paths, expands sibling `catchments.geojson` features, and stores one
 * resolved `BmiPipelineConfig` per feature.
 */
struct PartitionedBmiRealization {
    // Root directory for realization-driven runtime output
    std::string output_root_;

    // Resolved single-feature top-level BMI pipeline configs
    std::vector<BmiPipelineConfig> feature_pipeline_configs_;

    PartitionedBmiRealization() = default;

    /**
     * @brief Partition a NextGen-style realization file by feature.
     *
     * @param realization_path Path to the NextGen-style realization file.
     * @throws std::runtime_error if the realization is malformed or references
     * missing or unusable files.
     * @throws std::filesystem::filesystem_error if a referenced path cannot be
     * resolved or inspected.
     */
    explicit PartitionedBmiRealization(const std::string& realization_path);
};

/**
 * @brief Build one `Task` per partitioned BMI realization feature.
 * @param realization Partitioned BMI realization containing one
 * `BmiPipelineConfig` per feature.
 * @param simulation_unit_id Registered BMI simulation unit id for each task.
 * @return `Task`s ready to submit through `Runtime::submit_job()`.
 * @throws std::runtime_error if a feature contains invalid forcing task inputs.
 */
std::vector<Task> build_bmi_realization_tasks(const PartitionedBmiRealization& realization,
                                              SimulationUnitId simulation_unit_id);

/**
 * @brief Simulation unit that runs BMI models through the task pipeline.
 *
 * For ordinary linked BMI model types, `BmiSimulationUnit` constructs a fresh
 * model per task in the parent process and runs it inside a `BmiPipeline`. The
 * first task argument must store a serialized `BmiPipelineConfig`, usually built
 * by `make_bmi_model_config(...)` and converted with
 * `bmi_pipeline_config_to_value(...)`.
 *
 * For `BmiPipeline`, `BmiSimulationUnit` runs the complete pipeline from the
 * task's serialized `BmiPipelineConfig`. Subprocess execution is selected per
 * task with `Task::execution_mode_`, and the task's `Task::subprocess_path_`
 * can select a specific subprocess executable. Subprocess modes use the same
 * subprocess worker backend; `ExecutionMode::single_task_subprocess` stops the
 * worker after one task, while `ExecutionMode::persistent_subprocess` reuses
 * the worker process owned by a `Runtime` job worker. Reuse applies only to the
 * subprocess process, not initialized BMI model instances.
 *
 * @tparam BmiModelType BMI model type derived from `bmi_api::Bmi`.
 */
template <class BmiModelType>
    requires std::derived_from<BmiModelType, bmi_api::Bmi>
class BmiSimulationUnit : public SimulationUnit {
  public:
    /**
     * @brief Construct a BMI simulation unit.
     *
     * Default-constructible linked model types store a factory for fresh
     * per-task model instances.
     */
    BmiSimulationUnit()
        requires(std::default_initializable<BmiModelType>)
    {
        // Default-constructible linked models use the simplest per-task factory
        model_factory_ = []() -> std::unique_ptr<BmiModelType> {
            return std::make_unique<BmiModelType>();
        };
    }

    /**
     * @brief Construct a model simulation unit from model constructor arguments.
     *
     * This overload is for `BmiSimulationUnit<SomeBmiModel>` where
     * `SomeBmiModel` is not `BmiPipeline`. MIAOW stores these arguments and
     * reuses them to construct a fresh `SomeBmiModel` instance for each task when
     * `model_factory_()` is called inside `execute()`.
     *
     * @tparam ModelConstructorArgs Constructor argument types for `BmiModelType`.
     * @param model_constructor_args Arguments forwarded to the BMI model constructor.
     */
    template <class... ModelConstructorArgs>
        requires(!std::same_as<BmiModelType, BmiPipeline> &&
                 std::constructible_from<BmiModelType,
                                         const std::decay_t<ModelConstructorArgs>&...>)
    explicit BmiSimulationUnit(ModelConstructorArgs... model_constructor_args) {
        // Constructor arguments are replayed for every fresh linked model
        model_factory_ = make_model_factory(std::move(model_constructor_args)...);
    }

    /**
     * @brief Execute a BMI model instance for a single task.
     * @param task `Task` whose first argument is a BMI pipeline config.
     * @return Structured `TaskResult` for this BMI task execution.
     */
    TaskResult execute(const Task& task, ExecutionContext&) override {
        // Linked model units must supply the model instance in this process
        if (task.execution_mode_ != ExecutionMode::in_process) {
            return make_failed_task_result(
                "subprocess execution requires BmiSimulationUnit<BmiPipeline>",
                "Subprocess error");
        }

        try {
            // Construct a BMI pipeline with the BmiSimulationUnit's model factory
            std::unique_ptr<BmiModelType> bmi_model = model_factory_();
            BmiPipeline pipeline(task, std::move(bmi_model));
            return pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions become failed task results so the job can keep running
            return make_failed_task_result(std::current_exception(), "BMI error");
        }
    }

  private:
    // Linked model units create one BMI model object per task
    using ModelFactory = std::function<std::unique_ptr<BmiModelType>()>;

    template <class... ModelConstructorArgs>
    static ModelFactory make_model_factory(ModelConstructorArgs... model_constructor_args) {
        // Store constructor arguments outside the task payload
        std::tuple<std::decay_t<ModelConstructorArgs>...> stored_constructor_args(
            std::move(model_constructor_args)...);

        return [stored_constructor_args =
                    std::move(stored_constructor_args)]() -> std::unique_ptr<BmiModelType> {
            // Reconstruct the concrete linked model from the captured arguments
            return std::apply(
                [](const std::decay_t<ModelConstructorArgs>&... args)
                    -> std::unique_ptr<BmiModelType> {
                    return std::make_unique<BmiModelType>(args...);
                },
                stored_constructor_args);
        };
    }

    ModelFactory model_factory_;
};

/**
 * @brief Simulation unit specialization for serialized `BmiPipeline` tasks.
 */
template <>
class BmiSimulationUnit<BmiPipeline> : public SimulationUnit {
  public:
    // Serialized pipeline tasks carry all model construction data in the task
    BmiSimulationUnit() = default;

    /**
     * @brief Execute a serialized BMI pipeline task.
     * @param task `Task` whose first argument is a BMI pipeline config.
     * @param context Runtime-local services for this worker task execution.
     * @return Structured `TaskResult` for this BMI pipeline task execution.
     */
    TaskResult execute(const Task& task, ExecutionContext& context) override {
        // Subprocess modes run the serialized BMI pipeline in the subprocess
        if (task.execution_mode_ == ExecutionMode::single_task_subprocess ||
            task.execution_mode_ == ExecutionMode::persistent_subprocess) {
            return context.execute_subprocess(task);
        }

        try {
            // In-process pipeline tasks construct all configured model modules here
            BmiPipeline pipeline(task);
            return pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions become failed task results so the job can keep running
            return make_failed_task_result(std::current_exception(), "BMI error");
        }
    }
};

} // namespace miaow
