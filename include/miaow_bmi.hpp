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
 * @brief C++ BMI model backed by a dynamically loaded BMI plugin.
 *
 * The plugin path is used exactly as provided. The plugin must export the
 * C-callable `register_bmi(void**)` symbol expected by NOAA's
 * `iso_c_fortran_bmi` bridge. The loaded model is exposed to MIAOW through the
 * C++ BMI interface and can be used with the BmiSimulationUnit in the same way as a 
 * statically linked BMI model.
 */
class DynamicBmiModel : public bmi_api::Bmi {
public:
    /**
     * @brief Open a dynamic BMI plugin from an explicit filesystem path.
     * @param plugin_path Path passed directly to the platform dynamic loader.
     */
    explicit DynamicBmiModel(std::string plugin_path);

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
    using RegisterBmiFunction = int (*)(void**);

    std::string plugin_path_;
    void* plugin_handle_{nullptr};
    void* model_handle_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
};

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
        } catch (const std::exception& exception) {
            TaskResult task_result;
            task_result.simulation_unit_id_ = task.simulation_unit_id_;
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = std::string("BMI error: ") + exception.what();
            return task_result;
        } catch (...) {
            TaskResult task_result;
            task_result.simulation_unit_id_ = task.simulation_unit_id_;
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = "BMI error: unknown exception";
            return task_result;
        }
    }

private:
    std::function<std::unique_ptr<BmiModelType>()> model_factory_;
};

} // namespace miaow
