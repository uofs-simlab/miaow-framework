#pragma once

#include <string>
#include <vector>

#include "miaow_bmi.hpp"

namespace miaow {

// Load and release one plugin model; its caller owns initialization and finalization
class DynamicBmiModel : public bmi_api::Bmi {
  public:
    explicit DynamicBmiModel(const DynamicBmiConfig& config);

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

    void open_cpp_plugin(const DynamicBmiConfig& config);
    void open_fortran_plugin(const DynamicBmiConfig& config);
    void unregister_fortran_model();
    void cleanup_model();

    DynamicBmiBackend backend_;
    void* library_handle_{nullptr};
    bmi_api::Bmi* cpp_model_{nullptr};
    DestroyBmiFunction destroy_bmi_{nullptr};
    void* fortran_model_handle_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
    UnregisterBmiFunction unregister_bmi_{nullptr};
};

} // namespace miaow

namespace miaow::bmi {

// Load and execute one complete dynamic BMI task in the calling process
TaskResult execute_task(const Task& task);

} // namespace miaow::bmi
