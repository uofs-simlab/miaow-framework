#pragma once

#include <concepts>
#include <exception>
#include <functional>
#include <map>
#include <memory>
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
 * @brief Backend implemented by a dynamically loaded BMI plugin.
 *
 * Dynamic modules adapt plugins that implement the
 * [Basic Model Interface](https://bmi.csdms.io/en/stable/).
 */
enum class DynamicBmiBackend {
    cpp,
    fortran,
};

/**
 * @brief Configuration for one statically typed BMI task.
 */
struct StaticBmiConfig {
    // BMI initialization config path
    std::string init_config_;

    // Optional CSV forcing file path
    std::string forcing_file_;

    // Map from BMI input names to forcing CSV headers
    std::map<std::string, std::string> variables_names_map_;

    // BMI output variables written to task CSV output
    std::vector<std::string> output_variables_;
};

/**
 * @brief Configuration for one dynamically loaded BMI plugin task.
 */
struct DynamicBmiConfig {
    // Backend implemented by the plugin
    DynamicBmiBackend backend_{DynamicBmiBackend::cpp};

    // Dynamic plugin library path
    std::string plugin_path_;

    // Optional BMI initialization config path
    std::string init_config_;

    // Optional CSV forcing file path
    std::string forcing_file_;

    // Map from BMI input names to forcing CSV headers
    std::map<std::string, std::string> variables_names_map_;

    // BMI output variables written to task CSV output
    std::vector<std::string> output_variables_;

    // Execution location for this task
    ExecutionMode execution_mode_{ExecutionMode::in_process};

    // Optional subprocess executable path
    std::string subprocess_path_;
};

/**
 * @brief Build one task for a statically typed BMI model.
 * @param simulation_unit_id Registered static BMI simulation unit id.
 * @param config Static BMI task configuration.
 * @return Task ready to submit through `Runtime::submit_job()`.
 */
Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const StaticBmiConfig& config);

/**
 * @brief Build one task for a dynamically loaded BMI plugin.
 * @param simulation_unit_id Registered dynamic BMI simulation unit id.
 * @param config Dynamic BMI task configuration.
 * @return Task ready to submit through `Runtime::submit_job()`.
 */
Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const DynamicBmiConfig& config);

/**
 * @brief Build tasks from static or dynamic BMI configurations.
 * @tparam BmiConfig `StaticBmiConfig` or `DynamicBmiConfig`.
 * @param simulation_unit_id Registered BMI simulation unit id.
 * @param configs BMI task configurations.
 * @return Tasks ready to submit through `Runtime::submit_job()`.
 */
template <class BmiConfig>
    requires(std::same_as<BmiConfig, StaticBmiConfig> ||
             std::same_as<BmiConfig, DynamicBmiConfig>)
std::vector<Task> make_bmi_tasks(SimulationUnitId simulation_unit_id,
                                 const std::vector<BmiConfig>& configs) {
    std::vector<Task> tasks;
    tasks.reserve(configs.size());
    for (const BmiConfig& config : configs) {
        tasks.push_back(make_bmi_task(simulation_unit_id, config));
    }
    return tasks;
}

/**
 * @brief Internal task-level BMI execution pipeline.
 *
 * `BmiPipeline` owns the
 * [BMI lifecycle](https://bmi.csdms.io/en/stable/) for one task. It reads
 * internal task configuration, constructs a configured plugin model or
 * receives one model from `StaticBmiSimulationUnit`, connects forcing, writes
 * output, and reports artifacts through the task result.
 *
 * `StaticBmiSimulationUnit` and `DynamicBmiSimulationUnit` provide the public
 * task execution interface.
 */
class BmiPipeline : public bmi_api::Bmi {
  public:
    /**
     * @brief Construct an empty pipeline that can be initialized from a config path.
     * @param model Optional model provided by `StaticBmiSimulationUnit`.
     */
    explicit BmiPipeline(std::unique_ptr<bmi_api::Bmi> model = nullptr);

    /**
     * @brief Construct a task pipeline and write its initialization file.
     * @param task Task whose first argument stores internal BMI configuration.
     * @param model Optional model provided by `StaticBmiSimulationUnit`.
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
 * @brief NextGen-style realization expanded into BMI tasks by feature.
 *
 * The constructor reads a
 * [NextGen-style](https://github.com/NOAA-OWP/ngen) global realization with
 * one `bmi_fortran` module and `CsvPerFeature` forcing, resolves relative
 * paths and expands sibling `catchments.geojson` features.
 */
class PartitionedBmiRealization {
  public:
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

    /**
     * @brief Return the resolved runtime output root from the realization.
     */
    const std::string& output_root() const;

  private:
    struct FeatureConfig {
        DynamicBmiConfig config_;
        std::string feature_id_;
    };

    // Root directory for realization-driven runtime output
    std::string output_root_;

    // Resolved configuration and identifier for each feature
    std::vector<FeatureConfig> feature_configs_;

    friend std::vector<Task> make_bmi_tasks(
        SimulationUnitId simulation_unit_id, const PartitionedBmiRealization& realization,
        ExecutionMode execution_mode);
};

/**
 * @brief Build one dynamic BMI task per realization feature.
 * @param simulation_unit_id Registered dynamic BMI simulation unit id.
 * @param realization Parsed BMI realization.
 * @param execution_mode Execution mode applied to every generated task.
 * @return `Task`s ready to submit through `Runtime::submit_job()`.
 */
std::vector<Task>
make_bmi_tasks(SimulationUnitId simulation_unit_id, const PartitionedBmiRealization& realization,
               ExecutionMode execution_mode = ExecutionMode::in_process);

/**
 * @brief Simulation unit for a statically typed BMI model.
 *
 * A fresh model is constructed for every task and run in the parent process.
 *
 * @tparam BmiModelType BMI model type derived from `bmi_api::Bmi`.
 */
template <class BmiModelType>
    requires std::derived_from<BmiModelType, bmi_api::Bmi>
class StaticBmiSimulationUnit : public SimulationUnit {
  public:
    /**
     * @brief Construct a BMI simulation unit.
     *
     * Default-constructible model types store a factory for fresh
     * per-task model instances.
     */
    StaticBmiSimulationUnit()
        requires(std::default_initializable<BmiModelType>)
    {
        // Default-constructible models use the simplest per-task factory
        model_factory_ = []() -> std::unique_ptr<BmiModelType> {
            return std::make_unique<BmiModelType>();
        };
    }

    /**
     * @brief Construct a model simulation unit from model constructor arguments.
     *
     * MIAOW stores these arguments and reuses them to construct a fresh model
     * instance for each task.
     *
     * @tparam ModelConstructorArgs Constructor argument types for `BmiModelType`.
     * @param model_constructor_args Arguments forwarded to the BMI model constructor.
     */
    template <class... ModelConstructorArgs>
        requires(std::constructible_from<BmiModelType,
                                         const std::decay_t<ModelConstructorArgs>&...>)
    explicit StaticBmiSimulationUnit(ModelConstructorArgs... model_constructor_args) {
        // Constructor arguments are replayed for every fresh linked model
        model_factory_ = make_model_factory(std::move(model_constructor_args)...);
    }

    /**
     * @brief Execute a BMI model instance for a single task.
     * @param task Task built from `StaticBmiConfig`.
     * @return Structured `TaskResult` for this BMI task execution.
     */
    TaskResult execute(const Task& task, ExecutionContext&) override {
        // Static model units must supply the model instance in this process
        if (task.execution_mode_ != ExecutionMode::in_process) {
            return make_failed_task_result(
                "subprocess execution requires DynamicBmiSimulationUnit",
                "Subprocess error");
        }

        try {
            // Construct a BMI pipeline with the static unit's model factory
            std::unique_ptr<BmiModelType> bmi_model = model_factory_();
            BmiPipeline pipeline(task, std::move(bmi_model));
            return pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions become failed task results so the job can keep running
            return make_failed_task_result(std::current_exception(), "BMI error");
        }
    }

  private:
    // Static model units create one BMI model object per task
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
 * @brief Simulation unit for dynamically loaded BMI plugin tasks.
 */
class DynamicBmiSimulationUnit : public SimulationUnit {
  public:
    DynamicBmiSimulationUnit() = default;

    /**
     * @brief Execute a dynamically loaded BMI plugin task.
     * @param task Task built from `DynamicBmiConfig`.
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
