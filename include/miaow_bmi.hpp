#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// To avoid confusion with the miaow::bmi namespace, we alias the third-party BMI API 
// namespace here
namespace bmi_api = ::bmi;

// Public bridge from header template code to compiled BMI execution logic
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
 * @brief C++ BMI model backed by a dynamically loaded C++ BMI plugin.
 *
 * The plugin must export `register_bmi(bmi::Bmi**)` and
 * `unregister_bmi(bmi::Bmi**)`.
 */
template <>
class DynamicBmi<DynamicBmiBackend::cpp> : public bmi_api::Bmi {
public:
    /**
     * @brief Open a dynamic C++ BMI plugin from an explicit filesystem path.
     * @param plugin_path Path passed directly to the platform dynamic loader.
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
    struct State;
    std::unique_ptr<State> state_;
};

/**
 * @brief C++ BMI model backed by a dynamically loaded Fortran BMI plugin.
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
    struct State;
    std::unique_ptr<State> state_;
};

using DynamicCppBmi = DynamicBmi<DynamicBmiBackend::cpp>;
using DynamicFortranBmi = DynamicBmi<DynamicBmiBackend::fortran>;

/**
 * @brief Execute one dynamic BMI task in a subprocess.
 */
TaskResult execute_dynamic_bmi_model_in_subprocess(
    DynamicBmiBackend backend,
    const std::string& plugin_path,
    const Task& task);

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from bmi_api::Bmi.
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
    explicit BmiSimulationUnit(
        std::string plugin_path,
        BmiExecutionMode execution_mode = BmiExecutionMode::in_process)
        : plugin_path_(std::move(plugin_path)),
          execution_mode_(execution_mode) {
    }

    TaskResult execute(const Task& task) override {
        const char* error_prefix = execution_mode_ == BmiExecutionMode::subprocess
            ? "BMI subprocess error"
            : "BMI error";

        try {
            // Option to execute in subprocess. This will launch the configured subprocess
            // executable and pass the plugin path and task config through a file-based
            // contract. The subprocess will construct the model, and execute it, and then 
            // return the result through another file-based contract.
            if (execution_mode_ == BmiExecutionMode::subprocess) {
                return execute_dynamic_bmi_model_in_subprocess(
                    Backend, plugin_path_, task);
            }

            // If user does not choose to execute in subprocess, default to in-process 
            // execution, which constructs the model from the plugin path right here, and 
            // calls execute_bmi_model directly.
            DynamicBmi<Backend> bmi_model(plugin_path_);
            return execute_bmi_model(bmi_model, task);
        } catch (...) {
            // If something did not go as planned, make a failed task result
            return make_failed_task_result(
                task,
                std::current_exception(),
                error_prefix);
        }
    }

private:
    std::string plugin_path_;
    BmiExecutionMode execution_mode_;
};

} // namespace miaow
