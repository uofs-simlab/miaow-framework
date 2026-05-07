#include "miaow_bmi.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <dlfcn.h>
#include <memory>
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

using BmiStringBuffer = std::array<char, max_bmi_string_length>;
using BmiNamesFunction = int (*)(void**, char**);

void check_bmi_status(int status, const std::string& operation) {
    if (status != bmi_success) {
        throw std::runtime_error(operation + " failed");
    }
}

BmiStringBuffer make_bmi_string(const std::string& value,
                                const std::string& context) {
    if (value.size() >= max_bmi_string_length) {
        throw std::runtime_error(context + " exceeds BMI string buffer length");
    }

    BmiStringBuffer buffer{};
    std::copy(value.begin(), value.end(), buffer.begin());
    return buffer;
}

std::vector<std::string> read_var_names(void** model_handle,
                                        int count,
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

// RAII wrapper for dlopen handles with symbol resolution and error handling
class DynamicLibrary {
public:
    DynamicLibrary(std::string plugin_path, std::string error_prefix)
        : plugin_path_(std::move(plugin_path)),
          error_prefix_(std::move(error_prefix)) {
        if (plugin_path_.empty()) {
            throw std::runtime_error(error_prefix_ + "plugin path is empty");
        }

        handle_ = dlopen(plugin_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            const char* error = dlerror();
            const std::string detail =
                error == nullptr ? "unknown loader error" : error;
            throw std::runtime_error(
                error_prefix_ + "unable to load plugin '" + plugin_path_ +
                "': " + detail);
        }
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    ~DynamicLibrary() {
        if (handle_ != nullptr) {
            (void)dlclose(handle_);
            handle_ = nullptr;
        }
    }

    void* resolve_required_symbol(const std::string& symbol_name) const {
        void* symbol = resolve_symbol(symbol_name);
        if (symbol == nullptr) {
            throw std::runtime_error(
                error_prefix_ + "unable to resolve symbol '" + symbol_name +
                "': symbol not found");
        }
        return symbol;
    }

    void* resolve_optional_symbol(const std::string& symbol_name) const {
        return resolve_symbol(symbol_name);
    }

private:
    void* resolve_symbol(const std::string& symbol_name) const {
        dlerror();
        void* symbol = dlsym(handle_, symbol_name.c_str());
        const char* error = dlerror();
        if (error != nullptr) {
            return nullptr;
        }
        return symbol;
    }

    std::string plugin_path_;
    std::string error_prefix_;
    void* handle_{nullptr};
};

} // namespace

// Implementation of DynamicBmi for the C++ dynamic plugin backend
struct DynamicBmi<DynamicBmiBackend::cpp>::State {
    using CreateBmiFunction = bmi_api::Bmi* (*)();
    using DestroyBmiFunction = void (*)(bmi_api::Bmi*);

    State(std::string plugin_path,
          std::string create_function,
          std::string destroy_function)
        : plugin_path_(std::move(plugin_path)),
          create_function_(std::move(create_function)),
          destroy_function_(std::move(destroy_function)),
          library_(plugin_path_, "BMI dynamic C++ plugin error: ") {
        load_create_destroy();
    }

    ~State() {
        cleanup_model();
    }

    void load_create_destroy() {
        if (create_function_.empty() || destroy_function_.empty()) {
            throw std::runtime_error(
                "BMI dynamic C++ plugin error: create/destroy symbol is empty");
        }

        void* create_symbol =
            library_.resolve_required_symbol(create_function_);
        create_bmi_ = reinterpret_cast<CreateBmiFunction>(create_symbol);

        void* destroy_symbol =
            library_.resolve_required_symbol(destroy_function_);
        destroy_bmi_ = reinterpret_cast<DestroyBmiFunction>(destroy_symbol);

        model_ = create_bmi_();
        if (model_ == nullptr) {
            throw std::runtime_error(
                "BMI dynamic C++ plugin error: create function returned null model");
        }
    }

    void cleanup_model() {
        if (model_ == nullptr) {
            return;
        }
        if (initialized_) {
            try {
                model_->Finalize();
            } catch (...) {
            }
            initialized_ = false;
        }

        destroy_bmi_(model_);
        model_ = nullptr;
    }

    std::string plugin_path_;
    std::string create_function_;
    std::string destroy_function_;
    DynamicLibrary library_;
    bmi_api::Bmi* model_{nullptr};
    CreateBmiFunction create_bmi_{nullptr};
    DestroyBmiFunction destroy_bmi_{nullptr};
    bool initialized_{false};
};

DynamicBmi<DynamicBmiBackend::cpp>::DynamicBmi(
    std::string plugin_path,
    std::string create_function,
    std::string destroy_function)
    : state_(std::make_unique<State>(
          std::move(plugin_path),
          std::move(create_function),
          std::move(destroy_function))) {
}

DynamicBmi<DynamicBmiBackend::cpp>::~DynamicBmi() = default;

void DynamicBmi<DynamicBmiBackend::cpp>::Initialize(std::string config_file) {
    state_->model_->Initialize(std::move(config_file));
    state_->initialized_ = true;
}

void DynamicBmi<DynamicBmiBackend::cpp>::Update() {
    state_->model_->Update();
}

void DynamicBmi<DynamicBmiBackend::cpp>::UpdateUntil(double time) {
    state_->model_->UpdateUntil(time);
}

void DynamicBmi<DynamicBmiBackend::cpp>::Finalize() {
    state_->model_->Finalize();
    state_->initialized_ = false;
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetComponentName() {
    return state_->model_->GetComponentName();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetInputItemCount() {
    return state_->model_->GetInputItemCount();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetOutputItemCount() {
    return state_->model_->GetOutputItemCount();
}

std::vector<std::string>
DynamicBmi<DynamicBmiBackend::cpp>::GetInputVarNames() {
    return state_->model_->GetInputVarNames();
}

std::vector<std::string>
DynamicBmi<DynamicBmiBackend::cpp>::GetOutputVarNames() {
    return state_->model_->GetOutputVarNames();
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarGrid(std::string name) {
    return state_->model_->GetVarGrid(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarType(
    std::string name) {
    return state_->model_->GetVarType(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarUnits(
    std::string name) {
    return state_->model_->GetVarUnits(std::move(name));
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarItemsize(std::string name) {
    return state_->model_->GetVarItemsize(std::move(name));
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetVarNbytes(std::string name) {
    return state_->model_->GetVarNbytes(std::move(name));
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetVarLocation(
    std::string name) {
    return state_->model_->GetVarLocation(std::move(name));
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetCurrentTime() {
    return state_->model_->GetCurrentTime();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetStartTime() {
    return state_->model_->GetStartTime();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetEndTime() {
    return state_->model_->GetEndTime();
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetTimeUnits() {
    return state_->model_->GetTimeUnits();
}

double DynamicBmi<DynamicBmiBackend::cpp>::GetTimeStep() {
    return state_->model_->GetTimeStep();
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetValue(std::string name,
                                                       void* dest) {
    state_->model_->GetValue(std::move(name), dest);
}

void* DynamicBmi<DynamicBmiBackend::cpp>::GetValuePtr(std::string name) {
    return state_->model_->GetValuePtr(std::move(name));
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetValueAtIndices(
    std::string name,
    void* dest,
    int* inds,
    int count) {
    state_->model_->GetValueAtIndices(std::move(name), dest, inds, count);
}

void DynamicBmi<DynamicBmiBackend::cpp>::SetValue(std::string name,
                                                       void* src) {
    state_->model_->SetValue(std::move(name), src);
}

void DynamicBmi<DynamicBmiBackend::cpp>::SetValueAtIndices(
    std::string name,
    int* inds,
    int count,
    void* src) {
    state_->model_->SetValueAtIndices(std::move(name), inds, count, src);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridRank(const int grid) {
    return state_->model_->GetGridRank(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridSize(const int grid) {
    return state_->model_->GetGridSize(grid);
}

std::string DynamicBmi<DynamicBmiBackend::cpp>::GetGridType(
    const int grid) {
    return state_->model_->GetGridType(grid);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridShape(const int grid,
                                                           int* shape) {
    state_->model_->GetGridShape(grid, shape);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridSpacing(const int grid,
                                                             double* spacing) {
    state_->model_->GetGridSpacing(grid, spacing);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridOrigin(const int grid,
                                                            double* origin) {
    state_->model_->GetGridOrigin(grid, origin);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridX(const int grid,
                                                       double* x) {
    state_->model_->GetGridX(grid, x);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridY(const int grid,
                                                       double* y) {
    state_->model_->GetGridY(grid, y);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridZ(const int grid,
                                                       double* z) {
    state_->model_->GetGridZ(grid, z);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridNodeCount(const int grid) {
    return state_->model_->GetGridNodeCount(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridEdgeCount(const int grid) {
    return state_->model_->GetGridEdgeCount(grid);
}

int DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceCount(const int grid) {
    return state_->model_->GetGridFaceCount(grid);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridEdgeNodes(
    const int grid,
    int* edge_nodes) {
    state_->model_->GetGridEdgeNodes(grid, edge_nodes);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceEdges(
    const int grid,
    int* face_edges) {
    state_->model_->GetGridFaceEdges(grid, face_edges);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridFaceNodes(
    const int grid,
    int* face_nodes) {
    state_->model_->GetGridFaceNodes(grid, face_nodes);
}

void DynamicBmi<DynamicBmiBackend::cpp>::GetGridNodesPerFace(
    const int grid,
    int* nodes_per_face) {
    state_->model_->GetGridNodesPerFace(grid, nodes_per_face);
}

// Implementation of DynamicBmi for the Fortran dynamic plugin backend
struct DynamicBmi<DynamicBmiBackend::fortran>::State {
    using RegisterBmiFunction = int (*)(void**);
    using UnregisterBmiFunction = int (*)(void**);

    explicit State(std::string plugin_path)
        : library_(std::move(plugin_path), "BMI dynamic Fortran plugin error: ") {
        void* register_symbol = library_.resolve_required_symbol("register_bmi");
        register_bmi_ = reinterpret_cast<RegisterBmiFunction>(register_symbol);

        void* unregister_symbol = library_.resolve_optional_symbol("unregister_bmi");
        if (unregister_symbol != nullptr) {
            unregister_bmi_ =
                reinterpret_cast<UnregisterBmiFunction>(unregister_symbol);
        }
    }

    ~State() {
        cleanup_model();
    }

    void finalize_model() {
        if (model_handle_ != nullptr && initialized_) {
            check_bmi_status(finalize(&model_handle_), "finalize");
            initialized_ = false;
        }
    }

    void unregister_model() {
        if (model_handle_ == nullptr) {
            return;
        }
        if (unregister_bmi_ != nullptr) {
            (void)unregister_bmi_(&model_handle_);
        }
        model_handle_ = nullptr;
    }

    void cleanup_model() {
        if (model_handle_ == nullptr) {
            return;
        }
        if (initialized_) {
            (void)finalize(&model_handle_);
            initialized_ = false;
        }
        unregister_model();
    }

    DynamicLibrary library_;
    void* model_handle_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
    UnregisterBmiFunction unregister_bmi_{nullptr};
    bool initialized_{false};
};

DynamicBmi<DynamicBmiBackend::fortran>::DynamicBmi(
    std::string plugin_path)
    : state_(std::make_unique<State>(std::move(plugin_path))) {
}

DynamicBmi<DynamicBmiBackend::fortran>::~DynamicBmi() = default;

void DynamicBmi<DynamicBmiBackend::fortran>::Initialize(
    std::string config_file) {
    if (state_->model_handle_ == nullptr) {
        check_bmi_status(state_->register_bmi_(&state_->model_handle_),
                         "register_bmi");
    }

    BmiStringBuffer config_file_buffer =
        make_bmi_string(config_file, "BMI config path");
    check_bmi_status(
        initialize(&state_->model_handle_, config_file_buffer.data()),
        "initialize");
    state_->initialized_ = true;
}

void DynamicBmi<DynamicBmiBackend::fortran>::Update() {
    check_bmi_status(update(&state_->model_handle_), "update");
}

void DynamicBmi<DynamicBmiBackend::fortran>::UpdateUntil(double time) {
    check_bmi_status(update_until(&state_->model_handle_, &time), "update_until");
}

void DynamicBmi<DynamicBmiBackend::fortran>::Finalize() {
    state_->finalize_model();
    state_->unregister_model();
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetComponentName() {
    BmiStringBuffer name{};
    check_bmi_status(get_component_name(&state_->model_handle_, name.data()),
                     "get_component_name");
    return name.data();
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetInputItemCount() {
    int count = 0;
    check_bmi_status(get_input_item_count(&state_->model_handle_, &count),
                     "get_input_item_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetOutputItemCount() {
    int count = 0;
    check_bmi_status(get_output_item_count(&state_->model_handle_, &count),
                     "get_output_item_count");
    return count;
}

std::vector<std::string>
DynamicBmi<DynamicBmiBackend::fortran>::GetInputVarNames() {
    return read_var_names(&state_->model_handle_,
                          GetInputItemCount(),
                          get_input_var_names,
                          "get_input_var_names");
}

std::vector<std::string>
DynamicBmi<DynamicBmiBackend::fortran>::GetOutputVarNames() {
    return read_var_names(&state_->model_handle_,
                          GetOutputItemCount(),
                          get_output_var_names,
                          "get_output_var_names");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarGrid(std::string name) {
    int grid = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_grid(&state_->model_handle_, name_buffer.data(), &grid),
        "get_var_grid");
    return grid;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarType(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer type{};
    check_bmi_status(
        get_var_type(&state_->model_handle_, name_buffer.data(), type.data()),
        "get_var_type");

    // Normalize common Fortran BMI type names to C++ BMI type names
    const std::string raw_type = type.data();
    std::string normalized_type = raw_type;
    std::transform(normalized_type.begin(),
                   normalized_type.end(),
                   normalized_type.begin(),
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

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarUnits(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer units{};
    check_bmi_status(
        get_var_units(&state_->model_handle_, name_buffer.data(), units.data()),
        "get_var_units");
    return units.data();
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarItemsize(
    std::string name) {
    int size = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_itemsize(&state_->model_handle_, name_buffer.data(), &size),
        "get_var_itemsize");
    return size;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetVarNbytes(
    std::string name) {
    int nbytes = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_nbytes(&state_->model_handle_, name_buffer.data(), &nbytes),
        "get_var_nbytes");
    return nbytes;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetVarLocation(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer location{};
    check_bmi_status(
        get_var_location(
            &state_->model_handle_, name_buffer.data(), location.data()),
        "get_var_location");
    return location.data();
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetCurrentTime() {
    double time = 0.0;
    check_bmi_status(get_current_time(&state_->model_handle_, &time),
                     "get_current_time");
    return time;
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetStartTime() {
    double time = 0.0;
    check_bmi_status(get_start_time(&state_->model_handle_, &time),
                     "get_start_time");
    return time;
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetEndTime() {
    double time = 0.0;
    check_bmi_status(get_end_time(&state_->model_handle_, &time),
                     "get_end_time");
    return time;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetTimeUnits() {
    BmiStringBuffer units{};
    check_bmi_status(get_time_units(&state_->model_handle_, units.data()),
                     "get_time_units");
    return units.data();
}

double DynamicBmi<DynamicBmiBackend::fortran>::GetTimeStep() {
    double time_step = 0.0;
    check_bmi_status(get_time_step(&state_->model_handle_, &time_step),
                     "get_time_step");
    return time_step;
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetValue(std::string name,
                                                           void* dest) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            get_value_int(
                &state_->model_handle_, name_buffer.data(), static_cast<int*>(dest)),
            "get_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_float(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<float*>(dest)),
            "get_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_double(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<double*>(dest)),
            "get_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void* DynamicBmi<DynamicBmiBackend::fortran>::GetValuePtr(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    void* value_ptr = nullptr;
    if (type == "int") {
        check_bmi_status(
            get_value_ptr_int(
                &state_->model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_int");
        return value_ptr;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_ptr_float(
                &state_->model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_float");
        return value_ptr;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_ptr_double(
                &state_->model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_double");
        return value_ptr;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetValueAtIndices(
    std::string name,
    void* dest,
    int* inds,
    int count) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            get_value_at_indices_int(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<int*>(dest),
                inds),
            "get_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_at_indices_float(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<float*>(dest),
                inds),
            "get_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_at_indices_double(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<double*>(dest),
                inds),
            "get_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::SetValue(std::string name,
                                                           void* src) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            set_value_int(
                &state_->model_handle_, name_buffer.data(), static_cast<int*>(src)),
            "set_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            set_value_float(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<float*>(src)),
            "set_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            set_value_double(
                &state_->model_handle_,
                name_buffer.data(),
                static_cast<double*>(src)),
            "set_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmi<DynamicBmiBackend::fortran>::SetValueAtIndices(
    std::string name,
    int* inds,
    int count,
    void* src) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            set_value_at_indices_int(
                &state_->model_handle_,
                name_buffer.data(),
                inds,
                static_cast<int*>(src)),
            "set_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            set_value_at_indices_float(
                &state_->model_handle_,
                name_buffer.data(),
                inds,
                static_cast<float*>(src)),
            "set_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            set_value_at_indices_double(
                &state_->model_handle_,
                name_buffer.data(),
                inds,
                static_cast<double*>(src)),
            "set_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridRank(const int grid) {
    int mutable_grid = grid;
    int rank = 0;
    check_bmi_status(get_grid_rank(&state_->model_handle_, &mutable_grid, &rank),
                     "get_grid_rank");
    return rank;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridSize(const int grid) {
    int mutable_grid = grid;
    int size = 0;
    check_bmi_status(get_grid_size(&state_->model_handle_, &mutable_grid, &size),
                     "get_grid_size");
    return size;
}

std::string DynamicBmi<DynamicBmiBackend::fortran>::GetGridType(
    const int grid) {
    int mutable_grid = grid;
    BmiStringBuffer type{};
    check_bmi_status(
        get_grid_type(&state_->model_handle_, &mutable_grid, type.data()),
        "get_grid_type");
    return type.data();
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridShape(
    const int grid,
    int* shape) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_shape(&state_->model_handle_, &mutable_grid, shape),
        "get_grid_shape");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridSpacing(
    const int grid,
    double* spacing) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_spacing(&state_->model_handle_, &mutable_grid, spacing),
        "get_grid_spacing");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridOrigin(
    const int grid,
    double* origin) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_origin(&state_->model_handle_, &mutable_grid, origin),
        "get_grid_origin");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridX(const int grid,
                                                           double* x) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_x(&state_->model_handle_, &mutable_grid, x),
                     "get_grid_x");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridY(const int grid,
                                                           double* y) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_y(&state_->model_handle_, &mutable_grid, y),
                     "get_grid_y");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridZ(const int grid,
                                                           double* z) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_z(&state_->model_handle_, &mutable_grid, z),
                     "get_grid_z");
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridNodeCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_node_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_node_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridEdgeCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_edge_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_edge_count");
    return count;
}

int DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_face_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_face_count");
    return count;
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridEdgeNodes(
    const int grid,
    int* edge_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_edge_nodes(&state_->model_handle_, &mutable_grid, edge_nodes),
        "get_grid_edge_nodes");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceEdges(
    const int grid,
    int* face_edges) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_edges(&state_->model_handle_, &mutable_grid, face_edges),
        "get_grid_face_edges");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridFaceNodes(
    const int grid,
    int* face_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_nodes(&state_->model_handle_, &mutable_grid, face_nodes),
        "get_grid_face_nodes");
}

void DynamicBmi<DynamicBmiBackend::fortran>::GetGridNodesPerFace(
    const int grid,
    int* nodes_per_face) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_nodes_per_face(
            &state_->model_handle_, &mutable_grid, nodes_per_face),
        "get_grid_nodes_per_face");
}

} // namespace miaow
