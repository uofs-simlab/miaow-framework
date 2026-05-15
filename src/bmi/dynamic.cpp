#include "miaow_bmi.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// C functions exported by NOAA's iso_c_fortran_bmi interoperability layer
extern "C" {
int initialize(void** handle, char* config_file);
int update(void** handle);
int update_until(void** handle, double* time);
int finalize(void** handle);

int get_component_name(void** handle, char* name);
int get_input_item_count(void** handle, int* count);
int get_output_item_count(void** handle, int* count);
int get_input_var_names(void** handle, char** names);
int get_output_var_names(void** handle, char** names);

int get_var_grid(void** handle, char* name, int* grid);
int get_var_type(void** handle, char* name, char* type);
int get_var_units(void** handle, char* name, char* units);
int get_var_itemsize(void** handle, char* name, int* size);
int get_var_nbytes(void** handle, char* name, int* nbytes);
int get_var_location(void** handle, char* name, char* location);

int get_current_time(void** handle, double* time);
int get_start_time(void** handle, double* time);
int get_end_time(void** handle, double* time);
int get_time_units(void** handle, char* units);
int get_time_step(void** handle, double* time_step);

int get_value_int(void** handle, char* name, int* dest);
int get_value_float(void** handle, char* name, float* dest);
int get_value_double(void** handle, char* name, double* dest);
int get_value_ptr_int(void** handle, char* name, void** dest_ptr);
int get_value_ptr_float(void** handle, char* name, void** dest_ptr);
int get_value_ptr_double(void** handle, char* name, void** dest_ptr);
int get_value_at_indices_int(void** handle, char* name, int* dest, int* inds);
int get_value_at_indices_float(void** handle, char* name, float* dest, int* inds);
int get_value_at_indices_double(void** handle, char* name, double* dest, int* inds);

int set_value_int(void** handle, char* name, int* src);
int set_value_float(void** handle, char* name, float* src);
int set_value_double(void** handle, char* name, double* src);
int set_value_at_indices_int(void** handle, char* name, int* inds, int* src);
int set_value_at_indices_float(void** handle, char* name, int* inds, float* src);
int set_value_at_indices_double(void** handle, char* name, int* inds, double* src);

int get_grid_rank(void** handle, int* grid, int* rank);
int get_grid_size(void** handle, int* grid, int* size);
int get_grid_type(void** handle, int* grid, char* type);
int get_grid_shape(void** handle, int* grid, int* shape);
int get_grid_spacing(void** handle, int* grid, double* spacing);
int get_grid_origin(void** handle, int* grid, double* origin);
int get_grid_x(void** handle, int* grid, double* x);
int get_grid_y(void** handle, int* grid, double* y);
int get_grid_z(void** handle, int* grid, double* z);
int get_grid_node_count(void** handle, int* grid, int* count);
int get_grid_edge_count(void** handle, int* grid, int* count);
int get_grid_face_count(void** handle, int* grid, int* count);
int get_grid_edge_nodes(void** handle, int* grid, int* edge_nodes);
int get_grid_face_edges(void** handle, int* grid, int* face_edges);
int get_grid_face_nodes(void** handle, int* grid, int* face_nodes);
int get_grid_nodes_per_face(void** handle, int* grid, int* nodes_per_face);
}

namespace miaow {

namespace {

constexpr int bmi_success = 0;
constexpr int max_bmi_string_length = 2048;
constexpr const char* cpp_plugin_error_prefix = "BMI dynamic C++ plugin error: ";
constexpr const char* fortran_plugin_error_prefix = "BMI dynamic Fortran plugin error: ";

using BmiStringBuffer = std::array<char, max_bmi_string_length>;
using BmiNamesFunction = int (*)(void**, char**);

void check_bmi_status(int status, const std::string& operation) {
    if (status != bmi_success) {
        throw std::runtime_error(operation + " failed");
    }
}

BmiStringBuffer make_bmi_string(const std::string& value, const std::string& context) {
    if (value.size() >= max_bmi_string_length) {
        throw std::runtime_error(context + " exceeds BMI string buffer length");
    }

    BmiStringBuffer buffer{};
    std::copy(value.begin(), value.end(), buffer.begin());
    return buffer;
}

std::vector<std::string> read_var_names(void** model_handle, int count,
                                        BmiNamesFunction read_names,
                                        const std::string& operation) {
    if (count < 0) {
        throw std::runtime_error(operation + " returned a negative item count");
    }

    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(count));
    if (count == 0) {
        return names;
    }

    std::vector<BmiStringBuffer> buffers(static_cast<std::size_t>(count));
    std::vector<char*> buffer_pointers;
    buffer_pointers.reserve(buffers.size());
    for (BmiStringBuffer& buffer : buffers) {
        buffer_pointers.push_back(buffer.data());
    }

    check_bmi_status(read_names(model_handle, buffer_pointers.data()), operation);
    for (const BmiStringBuffer& buffer : buffers) {
        names.emplace_back(buffer.data());
    }
    return names;
}

void* open_dynamic_library(const std::string& plugin_path,
                           const std::string& error_prefix) {
    if (plugin_path.empty()) {
        throw std::runtime_error(error_prefix + "plugin path is empty");
    }

    void* handle = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        const std::string detail = error == nullptr ? "unknown loader error" : error;
        throw std::runtime_error(error_prefix + "unable to load plugin '" + plugin_path +
                                 "': " + detail);
    }
    return handle;
}

void close_dynamic_library(void*& library_handle) {
    if (library_handle != nullptr) {
        (void)dlclose(library_handle);
        library_handle = nullptr;
    }
}

void* resolve_dynamic_symbol(void* library_handle, const std::string& symbol_name) {
    dlerror();
    void* symbol = dlsym(library_handle, symbol_name.c_str());
    const char* error = dlerror();
    if (error != nullptr) {
        return nullptr;
    }
    return symbol;
}

void* resolve_required_dynamic_symbol(void* library_handle, const std::string& symbol_name,
                                      const std::string& error_prefix) {
    void* symbol = resolve_dynamic_symbol(library_handle, symbol_name);
    if (symbol == nullptr) {
        throw std::runtime_error(error_prefix + "unable to resolve symbol '" + symbol_name +
                                 "': symbol not found");
    }
    return symbol;
}

} // namespace

// Implementation of DynamicBmi for the C++ dynamic plugin backend
DynamicBmi<DynamicBmiBackend::cpp>::DynamicBmi(std::string plugin_path,
                                               std::string create_function,
                                               std::string destroy_function) {
    if (create_function.empty() || destroy_function.empty()) {
        throw std::runtime_error(
            "BMI dynamic C++ plugin error: create/destroy symbol is empty");
    }

    library_handle_ = open_dynamic_library(plugin_path, cpp_plugin_error_prefix);

    try {
        void* create_symbol =
            resolve_required_dynamic_symbol(library_handle_, create_function,
                                            cpp_plugin_error_prefix);
        create_bmi_ = reinterpret_cast<CreateBmiFunction>(create_symbol);

        void* destroy_symbol =
            resolve_required_dynamic_symbol(library_handle_, destroy_function,
                                            cpp_plugin_error_prefix);
        destroy_bmi_ = reinterpret_cast<DestroyBmiFunction>(destroy_symbol);

        model_ = create_bmi_();
        if (model_ == nullptr) {
            throw std::runtime_error(
                "BMI dynamic C++ plugin error: create function returned null model");
        }
    } catch (...) {
        cleanup_model();
        close_dynamic_library(library_handle_);
        throw;
    }
}

DynamicBmi<DynamicBmiBackend::cpp>::~DynamicBmi() {
    cleanup_model();
    close_dynamic_library(library_handle_);
}

void DynamicBmi<DynamicBmiBackend::cpp>::cleanup_model() {
    if (model_ == nullptr) {
        return;
    }
    if (initialized_) {
        try {
            model_->Finalize();
        } catch (...) {}
        initialized_ = false;
    }

    destroy_bmi_(model_);
    model_ = nullptr;
}

void DynamicBmi<DynamicBmiBackend::cpp>::Initialize(std::string config_file) {
    model_->Initialize(std::move(config_file));
    initialized_ = true;
}

void DynamicBmi<DynamicBmiBackend::cpp>::Update() {
    model_->Update();
}

void DynamicBmi<DynamicBmiBackend::cpp>::UpdateUntil(double time) {
    model_->UpdateUntil(time);
}

void DynamicBmi<DynamicBmiBackend::cpp>::Finalize() {
    model_->Finalize();
    initialized_ = false;
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetComponentName() {
    return model_->GetComponentName();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetInputItemCount() {
    return model_->GetInputItemCount();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetOutputItemCount() {
    return model_->GetOutputItemCount();
}

std::vector<std::string> DynamicBmi<DynamicBmiBackend::cpp>::GetInputVarNames() {
    return model_->GetInputVarNames();
}

std::vector<std::string> DynamicBmi<DynamicBmiBackend::cpp>::GetOutputVarNames() {
    return model_->GetOutputVarNames();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarGrid(std::string name) {
    return model_->GetVarGrid(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarType(std::string name) {
    return model_->GetVarType(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarUnits(std::string name) {
    return model_->GetVarUnits(std::move(name));
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarItemsize(std::string name) {
    return model_->GetVarItemsize(std::move(name));
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarNbytes(std::string name) {
    return model_->GetVarNbytes(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarLocation(std::string name) {
    return model_->GetVarLocation(std::move(name));
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetCurrentTime() {
    return model_->GetCurrentTime();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetStartTime() {
    return model_->GetStartTime();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetEndTime() {
    return model_->GetEndTime();
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetTimeUnits() {
    return model_->GetTimeUnits();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetTimeStep() {
    return model_->GetTimeStep();
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetValue(std::string name, void* dest) {
    model_->GetValue(std::move(name), dest);
}

void* DynamicBmi<DynamicBmiBackend::cpp>::GetValuePtr(std::string name) {
    return model_->GetValuePtr(std::move(name));
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetValueAtIndices(std::string name, void* dest,
                                                           int* inds, int count) {
    model_->GetValueAtIndices(std::move(name), dest, inds, count);
}

void DynamicBmi<DynamicBmiBackend::cpp>::SetValue(std::string name, void* src) {
    model_->SetValue(std::move(name), src);
}

void DynamicBmi<DynamicBmiBackend::cpp>::SetValueAtIndices(std::string name, int* inds,
                                                           int count, void* src) {
    model_->SetValueAtIndices(std::move(name), inds, count, src);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridRank(const int grid) {
    return model_->GetGridRank(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridSize(const int grid) {
    return model_->GetGridSize(grid);
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetGridType(const int grid) {
    return model_->GetGridType(grid);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridShape(const int grid, int* shape) {
    model_->GetGridShape(grid, shape);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridSpacing(const int grid, double* spacing) {
    model_->GetGridSpacing(grid, spacing);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridOrigin(const int grid, double* origin) {
    model_->GetGridOrigin(grid, origin);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridX(const int grid, double* x) {
    model_->GetGridX(grid, x);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridY(const int grid, double* y) {
    model_->GetGridY(grid, y);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridZ(const int grid, double* z) {
    model_->GetGridZ(grid, z);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridNodeCount(const int grid) {
    return model_->GetGridNodeCount(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridEdgeCount(const int grid) {
    return model_->GetGridEdgeCount(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceCount(const int grid) {
    return model_->GetGridFaceCount(grid);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    model_->GetGridEdgeNodes(grid, edge_nodes);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceEdges(const int grid, int* face_edges) {
    model_->GetGridFaceEdges(grid, face_edges);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceNodes(const int grid, int* face_nodes) {
    model_->GetGridFaceNodes(grid, face_nodes);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridNodesPerFace(const int grid,
                                                             int* nodes_per_face) {
    model_->GetGridNodesPerFace(grid, nodes_per_face);
}

// Implementation of DynamicBmi for the Fortran dynamic plugin backend
DynamicBmi<DynamicBmiBackend::fortran>::DynamicBmi(std::string plugin_path)
    : library_handle_(open_dynamic_library(plugin_path, fortran_plugin_error_prefix)) {
    try {
        void* register_symbol =
            resolve_required_dynamic_symbol(library_handle_, "register_bmi",
                                            fortran_plugin_error_prefix);
        register_bmi_ = reinterpret_cast<RegisterBmiFunction>(register_symbol);

        void* unregister_symbol = resolve_dynamic_symbol(library_handle_, "unregister_bmi");
        if (unregister_symbol != nullptr) {
            unregister_bmi_ = reinterpret_cast<UnregisterBmiFunction>(unregister_symbol);
        }
    } catch (...) {
        close_dynamic_library(library_handle_);
        throw;
    }
}

DynamicBmi<DynamicBmiBackend::fortran>::~DynamicBmi() {
    cleanup_model();
    close_dynamic_library(library_handle_);
}

void DynamicBmi<DynamicBmiBackend::fortran>::finalize_model() {
    if (model_handle_ != nullptr && initialized_) {
        check_bmi_status(finalize(&model_handle_), "finalize");
        initialized_ = false;
    }
}

void DynamicBmi<DynamicBmiBackend::fortran>::unregister_model() {
    if (model_handle_ == nullptr) {
        return;
    }
    if (unregister_bmi_ != nullptr) {
        (void)unregister_bmi_(&model_handle_);
    }
    model_handle_ = nullptr;
}

void DynamicBmi<DynamicBmiBackend::fortran>::cleanup_model() {
    if (model_handle_ == nullptr) {
        return;
    }
    if (initialized_) {
        (void)finalize(&model_handle_);
        initialized_ = false;
    }
    unregister_model();
}

void DynamicBmi<DynamicBmiBackend::fortran>::Initialize(std::string config_file) {
    if (model_handle_ == nullptr) {
        check_bmi_status(register_bmi_(&model_handle_), "register_bmi");
    }

    BmiStringBuffer config_file_buffer = make_bmi_string(config_file, "BMI config path");
    check_bmi_status(initialize(&model_handle_, config_file_buffer.data()), "initialize");
    initialized_ = true;
}

void DynamicBmi<DynamicBmiBackend::fortran>::Update() {
    check_bmi_status(update(&model_handle_), "update");
}

void DynamicBmi<DynamicBmiBackend::fortran>::UpdateUntil(double time) {
    check_bmi_status(update_until(&model_handle_, &time), "update_until");
}

void DynamicBmi<DynamicBmiBackend::fortran>::Finalize() {
    finalize_model();
    unregister_model();
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetComponentName() {
    BmiStringBuffer name{};
    check_bmi_status(get_component_name(&model_handle_, name.data()), "get_component_name");
    return name.data();
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetInputItemCount() {
    int count = 0;
    check_bmi_status(get_input_item_count(&model_handle_, &count), "get_input_item_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetOutputItemCount() {
    int count = 0;
    check_bmi_status(get_output_item_count(&model_handle_, &count),
                     "get_output_item_count");
    return count;
}

std::vector<std::string> DynamicBmi<DynamicBmiBackend::fortran>::GetInputVarNames() {
    return read_var_names(&model_handle_, GetInputItemCount(), get_input_var_names,
                          "get_input_var_names");
}

std::vector<std::string> DynamicBmi<DynamicBmiBackend::fortran>::GetOutputVarNames() {
    return read_var_names(&model_handle_, GetOutputItemCount(), get_output_var_names,
                          "get_output_var_names");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarGrid(std::string name) {
    int grid = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_grid(&model_handle_, name_buffer.data(), &grid),
                     "get_var_grid");
    return grid;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarType(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer type{};
    check_bmi_status(get_var_type(&model_handle_, name_buffer.data(), type.data()),
                     "get_var_type");

    // Normalize common Fortran BMI type names to C++ BMI type names
    const std::string raw_type = type.data();
    std::string normalized_type = raw_type;
    std::transform(normalized_type.begin(), normalized_type.end(), normalized_type.begin(),
                   [](unsigned char character) -> char {
                       return static_cast<char>(std::tolower(character));
                   });
    if (normalized_type == "real" || normalized_type == "float") {
        return "float";
    }
    if (normalized_type == "integer" || normalized_type == "int") {
        return "int";
    }
    if (normalized_type == "double" || normalized_type == "float64") {
        return "double";
    }
    return raw_type;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarUnits(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer units{};
    check_bmi_status(get_var_units(&model_handle_, name_buffer.data(), units.data()),
                     "get_var_units");
    return units.data();
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarItemsize(std::string name) {
    int size = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_itemsize(&model_handle_, name_buffer.data(), &size),
                     "get_var_itemsize");
    return size;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarNbytes(std::string name) {
    int nbytes = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_nbytes(&model_handle_, name_buffer.data(), &nbytes),
                     "get_var_nbytes");
    return nbytes;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarLocation(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer location{};
    check_bmi_status(get_var_location(&model_handle_, name_buffer.data(), location.data()),
                     "get_var_location");
    return location.data();
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetCurrentTime() {
    double time = 0.0;
    check_bmi_status(get_current_time(&model_handle_, &time), "get_current_time");
    return time;
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetStartTime() {
    double time = 0.0;
    check_bmi_status(get_start_time(&model_handle_, &time), "get_start_time");
    return time;
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetEndTime() {
    double time = 0.0;
    check_bmi_status(get_end_time(&model_handle_, &time), "get_end_time");
    return time;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetTimeUnits() {
    BmiStringBuffer units{};
    check_bmi_status(get_time_units(&model_handle_, units.data()), "get_time_units");
    return units.data();
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetTimeStep() {
    double time_step = 0.0;
    check_bmi_status(get_time_step(&model_handle_, &time_step), "get_time_step");
    return time_step;
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetValue(std::string name, void* dest) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(get_value_int(&model_handle_, name_buffer.data(),
                                       static_cast<int*>(dest)),
                         "get_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(get_value_float(&model_handle_, name_buffer.data(),
                                         static_cast<float*>(dest)),
                         "get_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(get_value_double(&model_handle_, name_buffer.data(),
                                          static_cast<double*>(dest)),
                         "get_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void* DynamicBmi<DynamicBmiBackend::fortran>::GetValuePtr(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    void* value_ptr = nullptr;
    if (type == "int") {
        check_bmi_status(get_value_ptr_int(&model_handle_, name_buffer.data(), &value_ptr),
                         "get_value_ptr_int");
        return value_ptr;
    }
    if (type == "float") {
        check_bmi_status(get_value_ptr_float(&model_handle_, name_buffer.data(),
                                             &value_ptr),
                         "get_value_ptr_float");
        return value_ptr;
    }
    if (type == "double") {
        check_bmi_status(get_value_ptr_double(&model_handle_, name_buffer.data(),
                                              &value_ptr),
                         "get_value_ptr_double");
        return value_ptr;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetValueAtIndices(std::string name, void* dest,
                                                               int* inds, int count) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(get_value_at_indices_int(&model_handle_, name_buffer.data(),
                                                  static_cast<int*>(dest), inds),
                         "get_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(get_value_at_indices_float(&model_handle_, name_buffer.data(),
                                                    static_cast<float*>(dest), inds),
                         "get_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(get_value_at_indices_double(&model_handle_, name_buffer.data(),
                                                     static_cast<double*>(dest), inds),
                         "get_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::SetValue(std::string name, void* src) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(set_value_int(&model_handle_, name_buffer.data(),
                                       static_cast<int*>(src)),
                         "set_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(set_value_float(&model_handle_, name_buffer.data(),
                                         static_cast<float*>(src)),
                         "set_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(set_value_double(&model_handle_, name_buffer.data(),
                                          static_cast<double*>(src)),
                         "set_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::SetValueAtIndices(std::string name, int* inds,
                                                               int count, void* src) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(set_value_at_indices_int(&model_handle_, name_buffer.data(), inds,
                                                  static_cast<int*>(src)),
                         "set_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(set_value_at_indices_float(&model_handle_, name_buffer.data(),
                                                    inds, static_cast<float*>(src)),
                         "set_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(set_value_at_indices_double(&model_handle_, name_buffer.data(),
                                                     inds, static_cast<double*>(src)),
                         "set_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridRank(const int grid) {
    int mutable_grid = grid;
    int rank = 0;
    check_bmi_status(get_grid_rank(&model_handle_, &mutable_grid, &rank), "get_grid_rank");
    return rank;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridSize(const int grid) {
    int mutable_grid = grid;
    int size = 0;
    check_bmi_status(get_grid_size(&model_handle_, &mutable_grid, &size), "get_grid_size");
    return size;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetGridType(const int grid) {
    int mutable_grid = grid;
    BmiStringBuffer type{};
    check_bmi_status(get_grid_type(&model_handle_, &mutable_grid, type.data()),
                     "get_grid_type");
    return type.data();
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridShape(const int grid, int* shape) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_shape(&model_handle_, &mutable_grid, shape),
                     "get_grid_shape");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridSpacing(const int grid,
                                                            double* spacing) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_spacing(&model_handle_, &mutable_grid, spacing),
                     "get_grid_spacing");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridOrigin(const int grid, double* origin) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_origin(&model_handle_, &mutable_grid, origin),
                     "get_grid_origin");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridX(const int grid, double* x) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_x(&model_handle_, &mutable_grid, x), "get_grid_x");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridY(const int grid, double* y) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_y(&model_handle_, &mutable_grid, y), "get_grid_y");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridZ(const int grid, double* z) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_z(&model_handle_, &mutable_grid, z), "get_grid_z");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridNodeCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_node_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_node_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridEdgeCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_edge_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_edge_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_face_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_face_count");
    return count;
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridEdgeNodes(const int grid,
                                                              int* edge_nodes) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_edge_nodes(&model_handle_, &mutable_grid, edge_nodes),
                     "get_grid_edge_nodes");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceEdges(const int grid,
                                                              int* face_edges) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_face_edges(&model_handle_, &mutable_grid, face_edges),
                     "get_grid_face_edges");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceNodes(const int grid,
                                                              int* face_nodes) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_face_nodes(&model_handle_, &mutable_grid, face_nodes),
                     "get_grid_face_nodes");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridNodesPerFace(const int grid,
                                                                 int* nodes_per_face) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_nodes_per_face(&model_handle_, &mutable_grid, nodes_per_face),
                     "get_grid_nodes_per_face");
}

} // namespace miaow
