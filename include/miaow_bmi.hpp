#pragma once

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
 * @brief One feature entry from a BMI realization file.
 *
 * NextGen-style realizations are expanded into one Fortran BMI `Task` input
 * per catchment. Realization-driven C++ BMI modules are not supported yet.
 */
struct RealizationFeature {
    // Dynamic Fortran BMI library path for this feature
    std::string library_file_;

    // BMI initialization config file path for this feature
    std::string init_config_path_;

    // Feature-specific forcing CSV file path
    std::string forcing_file_;

    // Mapping from BMI input variable names to forcing CSV column names
    std::map<std::string, std::string> variables_names_map_;
};

/**
 * @brief Parsed BMI realization used to build one `Task` per feature.
 *
 * The supported realization format is a NextGen-style global realization
 * with one `bmi_fortran` module and `CsvPerFeature` forcing.
 */
struct RealizationConfig {
    // Root directory for realization-driven runtime output
    std::string output_root_;

    // Feature entries expanded from the realization features (e.g., catchments)
    std::vector<RealizationFeature> features_;

    RealizationConfig() = default;

    /**
     * @brief Construct a parsed realization from a NextGen-style file.
     *
     * Relative paths are resolved against the realization file's parent
     * directory. The parser expands sibling `catchments.geojson` features into
     * realization feature `Task`s.
     *
     * @param realization_path Path to a NextGen-style realization file.
     * @throws std::runtime_error if the realization is malformed or references
     * missing or unusable files.
     * @throws std::filesystem::filesystem_error if a referenced path cannot be
     * resolved or inspected.
     */
    explicit RealizationConfig(const std::string& realization_path);
};

/**
 * @brief Execute a complete BMI model lifecycle for one task.
 * @param model BMI-compatible model object to initialize and execute.
 * @param task `Task` input containing model config, forcing, and output paths.
 * @return Structured `TaskResult` for the BMI task execution.
 * @throws std::runtime_error if the task specification, forcing setup, or BMI
 * model lifecycle fails.
 */
TaskResult execute_bmi_model(bmi_api::Bmi& model, const Task& task);

/**
 * @brief Backend interface exposed by a dynamically loaded BMI plugin.
 */
enum class DynamicBmiBackend {
    cpp,
    fortran,
};

/**
 * @brief Process isolation mode for dynamically loaded BMI models.
 */
enum class BmiExecutionMode {
    in_process,
    subprocess,
};

template <DynamicBmiBackend Backend>
class DynamicBmi;

/**
 * @brief BMI model backed by a dynamically loaded C++ BMI plugin.
 *
 * The plugin must export `bmi_model_create()` and `bmi_model_destroy(...)`.
 * These symbols may be renamed through constructor arguments.
 */
template <>
class DynamicBmi<DynamicBmiBackend::cpp> : public bmi_api::Bmi {
public:
    /**
     * @brief Open a dynamic C++ BMI plugin from an explicit filesystem path.
     * @param plugin_path Path passed directly to the platform dynamic loader.
     * @param create_function Exported function used to create the BMI model.
     * @param destroy_function Exported function used to destroy the BMI model.
     * @throws std::runtime_error if the plugin path or required symbols are
     * invalid, or if model creation fails.
     */
    explicit DynamicBmi(
        std::string plugin_path,
        std::string create_function = "bmi_model_create",
        std::string destroy_function = "bmi_model_destroy");

    DynamicBmi(const DynamicBmi&) = delete;
    DynamicBmi& operator=(const DynamicBmi&) = delete;

    ~DynamicBmi() override;

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

    void cleanup_model();

    void* library_handle_{nullptr};
    bmi_api::Bmi* model_{nullptr};
    CreateBmiFunction create_bmi_{nullptr};
    DestroyBmiFunction destroy_bmi_{nullptr};
    bool initialized_{false};
};

/**
 * @brief BMI model backed by a dynamically loaded Fortran BMI plugin.
 *
 * The plugin must export `register_bmi(void**)` for the linked NOAA-OWP
 * procedural BMI bridge. If the plugin exports `unregister_bmi(void**)`, MIAOW
 * calls it after BMI finalization.
 */
template <>
class DynamicBmi<DynamicBmiBackend::fortran> : public bmi_api::Bmi {
public:
    /**
     * @brief Open a dynamic Fortran BMI plugin from an explicit filesystem path.
     * @param plugin_path Path passed directly to the platform dynamic loader.
     * @throws std::runtime_error if the plugin path is invalid or `register_bmi`
     * cannot be resolved.
     */
    explicit DynamicBmi(std::string plugin_path);

    DynamicBmi(const DynamicBmi&) = delete;
    DynamicBmi& operator=(const DynamicBmi&) = delete;

    ~DynamicBmi() override;

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
    using RegisterBmiFunction = int (*)(void**);
    using UnregisterBmiFunction = int (*)(void**);

    void finalize_model();
    void unregister_model();
    void cleanup_model();

    void* library_handle_{nullptr};
    void* model_handle_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
    UnregisterBmiFunction unregister_bmi_{nullptr};
    bool initialized_{false};
};

/**
 * @brief Alias for `DynamicBmi` using the C++ plugin backend.
 */
using DynamicCppBmi = DynamicBmi<DynamicBmiBackend::cpp>;

/**
 * @brief Alias for `DynamicBmi` using the Fortran plugin backend.
 */
using DynamicFortranBmi = DynamicBmi<DynamicBmiBackend::fortran>;

/**
 * @brief Execute one dynamic BMI task in a subprocess.
 * @param backend `DynamicBmiBackend` implemented by the plugin.
 * @param plugin_path Path passed to the subprocess dynamic loader.
 * @param task `Task` input containing model config, forcing, and output paths.
 * @param create_function C++ plugin function used to create the BMI model.
 * @param destroy_function C++ plugin function used to destroy the BMI model.
 * @return Structured `TaskResult` for the subprocess BMI task execution.
 * @throws std::runtime_error if the task specification or subprocess execution
 * setup fails.
 * @throws std::filesystem::filesystem_error if subprocess control paths cannot
 * be resolved or created.
 */
TaskResult execute_dynamic_bmi_model_in_subprocess(
    DynamicBmiBackend backend,
    const std::string& plugin_path,
    const Task& task,
    const std::string& create_function = "bmi_model_create",
    const std::string& destroy_function = "bmi_model_destroy");

/**
 * @brief Register dynamic Fortran BMI units and build realization feature `Task`s.
 * @param realization Parsed BMI `RealizationConfig`.
 * @param registry Registry receiving one unit per unique BMI library path.
 * @param execution_mode Process isolation mode used by each dynamic BMI task.
 * @return `Task`s ready to submit through `Runtime::submit_job()`.
 * @throws std::runtime_error if a feature contains invalid forcing task inputs.
 */
std::vector<Task> build_bmi_realization_tasks(
    const RealizationConfig& realization,
    SimulationUnitRegistry& registry,
    BmiExecutionMode execution_mode = BmiExecutionMode::subprocess);

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from `bmi_api::Bmi`.
 */
template <class BmiModelType>
class BmiSimulationUnit : public SimulationUnit {
public:
    static_assert(std::is_base_of_v<bmi_api::Bmi, BmiModelType>,
                  "BmiModelType must derive from bmi::Bmi");

    /**
     * @brief Construct a BMI simulation unit for a default-constructible model.
     */
    BmiSimulationUnit() {
        static_assert(std::is_default_constructible_v<BmiModelType>,
                      "BmiModelType must be default constructible");
        model_factory_ = []() -> std::unique_ptr<BmiModelType> {
            return std::make_unique<BmiModelType>();
        };
    }

    /**
     * @brief Store constructor arguments for each per-task BMI model instance.
     * @tparam ConstructorArgs Constructor argument types for `BmiModelType`.
     * @param constructor_args Arguments forwarded to the BMI model constructor.
     */
    template <class... ConstructorArgs>
    explicit BmiSimulationUnit(ConstructorArgs... constructor_args) {
        static_assert(std::is_constructible_v<BmiModelType, ConstructorArgs...>,
                      "BmiModelType must be constructible from ConstructorArgs");

        std::tuple<std::decay_t<ConstructorArgs>...> stored_constructor_args(
            std::move(constructor_args)...);
        model_factory_ =
            [stored_constructor_args = std::move(stored_constructor_args)]()
                -> std::unique_ptr<BmiModelType> {
                return std::apply(
                    [](const std::decay_t<ConstructorArgs>&... args)
                        -> std::unique_ptr<BmiModelType> {
                        return std::make_unique<BmiModelType>(args...);
                    },
                    stored_constructor_args);
            };
    }

    /**
     * @brief Execute a BMI model instance for a single task.
     * @param task `Task` input containing model config, forcing, and output paths.
     * @return Structured `TaskResult` for this BMI task execution.
     */
    TaskResult execute(const Task& task) override {
        try {
            std::unique_ptr<BmiModelType> bmi_model = model_factory_();
            return execute_bmi_model(*bmi_model, task);
        } catch (...) {
            return make_failed_task_result(
                task,
                std::current_exception(),
                "BMI error");
        }
    }

private:
    std::function<std::unique_ptr<BmiModelType>()> model_factory_;
};

/**
 * @brief Simulation unit for a dynamically loaded BMI model.
 *
 * Owns the plugin path explicitly so subprocess execution can pass it to the
 * subprocess executable without loading the plugin in the parent process.
 */
template <DynamicBmiBackend Backend>
class BmiSimulationUnit<DynamicBmi<Backend>> : public SimulationUnit {
public:
    /**
     * @brief Construct a dynamic BMI simulation unit.
     * @param plugin_path Path passed to the dynamic loader.
     * @param execution_mode Process isolation mode used by each task.
     */
    explicit BmiSimulationUnit(
        std::string plugin_path,
        BmiExecutionMode execution_mode = BmiExecutionMode::in_process)
        : plugin_path_(std::move(plugin_path)),
          execution_mode_(execution_mode) {
    }

    /**
     * @brief Construct a dynamic C++ BMI simulation unit with custom symbols.
     * @tparam B `DynamicBmiBackend` constrained to C++ plugins.
     * @param plugin_path Path passed to the dynamic loader.
     * @param create_function Exported function used to create the BMI model.
     * @param destroy_function Exported function used to destroy the BMI model.
     * @param execution_mode Process isolation mode used by each task.
     */
    template <DynamicBmiBackend B = Backend,
              typename = std::enable_if_t<B == DynamicBmiBackend::cpp>>
    explicit BmiSimulationUnit(
        std::string plugin_path,
        std::string create_function,
        std::string destroy_function,
        BmiExecutionMode execution_mode = BmiExecutionMode::in_process)
        : plugin_path_(std::move(plugin_path)),
          cpp_create_function_(std::move(create_function)),
          cpp_destroy_function_(std::move(destroy_function)),
          execution_mode_(execution_mode) {
    }

    /**
     * @brief Execute a dynamic BMI model for a single task.
     * @param task `Task` input containing model config, forcing, and output paths.
     * @return Structured `TaskResult` for this dynamic BMI task execution.
     */
    TaskResult execute(const Task& task) override {
        const char* error_prefix = execution_mode_ == BmiExecutionMode::subprocess
            ? "BMI subprocess error"
            : "BMI error";

        try {
            // Subprocess mode loads plugins only inside the worker process
            if (execution_mode_ == BmiExecutionMode::subprocess) {
                return execute_dynamic_bmi_model_in_subprocess(
                    Backend,
                    plugin_path_,
                    task,
                    cpp_create_function_,
                    cpp_destroy_function_);
            }

            // In-process mode constructs the model in this process
            if constexpr (Backend == DynamicBmiBackend::cpp) {
                DynamicBmi<Backend> bmi_model(
                    plugin_path_,
                    cpp_create_function_,
                    cpp_destroy_function_);
                return execute_bmi_model(bmi_model, task);
            } else {
                DynamicBmi<Backend> bmi_model(plugin_path_);
                return execute_bmi_model(bmi_model, task);
            }
        } catch (...) {
            return make_failed_task_result(
                task,
                std::current_exception(),
                error_prefix);
        }
    }

private:
    std::string plugin_path_;
    std::string cpp_create_function_{"bmi_model_create"};
    std::string cpp_destroy_function_{"bmi_model_destroy"};
    BmiExecutionMode execution_mode_;
};

} // namespace miaow
