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

std::string to_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) -> char {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string normalize_var_type(const std::string& raw_type) {
    const std::string type = to_lower(raw_type);
    if (type == "real" || type == "float") {
        return "float";
    }
    if (type == "integer" || type == "int") {
        return "int";
    }
    if (type == "double" || type == "float64") {
        return "double";
    }
    return raw_type;
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

struct DynamicBmiModel<DynamicBmiLanguage::cpp>::State {
    using RegisterBmiFunction = int (*)(bmi_api::Bmi**);
    using UnregisterBmiFunction = int (*)(bmi_api::Bmi**);

    explicit State(std::string plugin_path)
        : library_(std::move(plugin_path), "BMI dynamic C++ plugin error: ") {
        void* register_symbol = library_.resolve_required_symbol("register_bmi");
        register_bmi_ = reinterpret_cast<RegisterBmiFunction>(register_symbol);

        void* unregister_symbol =
            library_.resolve_required_symbol("unregister_bmi");
        unregister_bmi_ =
            reinterpret_cast<UnregisterBmiFunction>(unregister_symbol);

        check_bmi_status(register_bmi_(&model_), "register_bmi");
        if (model_ == nullptr) {
            throw std::runtime_error(
                "BMI dynamic C++ plugin error: register_bmi returned null model");
        }
    }

    ~State() {
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
        (void)unregister_bmi_(&model_);
        model_ = nullptr;
    }

    DynamicLibrary library_;
    bmi_api::Bmi* model_{nullptr};
    RegisterBmiFunction register_bmi_{nullptr};
    UnregisterBmiFunction unregister_bmi_{nullptr};
    bool initialized_{false};
};

DynamicBmiModel<DynamicBmiLanguage::cpp>::DynamicBmiModel(std::string plugin_path)
    : state_(std::make_unique<State>(std::move(plugin_path))) {
}

DynamicBmiModel<DynamicBmiLanguage::cpp>::~DynamicBmiModel() = default;

void DynamicBmiModel<DynamicBmiLanguage::cpp>::Initialize(std::string config_file) {
    state_->model_->Initialize(std::move(config_file));
    state_->initialized_ = true;
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::Update() {
    state_->model_->Update();
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::UpdateUntil(double time) {
    state_->model_->UpdateUntil(time);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::Finalize() {
    state_->model_->Finalize();
    state_->initialized_ = false;
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetComponentName() {
    return state_->model_->GetComponentName();
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetInputItemCount() {
    return state_->model_->GetInputItemCount();
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetOutputItemCount() {
    return state_->model_->GetOutputItemCount();
}

std::vector<std::string>
DynamicBmiModel<DynamicBmiLanguage::cpp>::GetInputVarNames() {
    return state_->model_->GetInputVarNames();
}

std::vector<std::string>
DynamicBmiModel<DynamicBmiLanguage::cpp>::GetOutputVarNames() {
    return state_->model_->GetOutputVarNames();
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarGrid(std::string name) {
    return state_->model_->GetVarGrid(std::move(name));
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarType(
    std::string name) {
    return state_->model_->GetVarType(std::move(name));
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarUnits(
    std::string name) {
    return state_->model_->GetVarUnits(std::move(name));
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarItemsize(std::string name) {
    return state_->model_->GetVarItemsize(std::move(name));
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarNbytes(std::string name) {
    return state_->model_->GetVarNbytes(std::move(name));
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetVarLocation(
    std::string name) {
    return state_->model_->GetVarLocation(std::move(name));
}

double DynamicBmiModel<DynamicBmiLanguage::cpp>::GetCurrentTime() {
    return state_->model_->GetCurrentTime();
}

double DynamicBmiModel<DynamicBmiLanguage::cpp>::GetStartTime() {
    return state_->model_->GetStartTime();
}

double DynamicBmiModel<DynamicBmiLanguage::cpp>::GetEndTime() {
    return state_->model_->GetEndTime();
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetTimeUnits() {
    return state_->model_->GetTimeUnits();
}

double DynamicBmiModel<DynamicBmiLanguage::cpp>::GetTimeStep() {
    return state_->model_->GetTimeStep();
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetValue(std::string name,
                                                       void* dest) {
    state_->model_->GetValue(std::move(name), dest);
}

void* DynamicBmiModel<DynamicBmiLanguage::cpp>::GetValuePtr(std::string name) {
    return state_->model_->GetValuePtr(std::move(name));
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetValueAtIndices(
    std::string name,
    void* dest,
    int* inds,
    int count) {
    state_->model_->GetValueAtIndices(std::move(name), dest, inds, count);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::SetValue(std::string name,
                                                       void* src) {
    state_->model_->SetValue(std::move(name), src);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::SetValueAtIndices(
    std::string name,
    int* inds,
    int count,
    void* src) {
    state_->model_->SetValueAtIndices(std::move(name), inds, count, src);
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridRank(const int grid) {
    return state_->model_->GetGridRank(grid);
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridSize(const int grid) {
    return state_->model_->GetGridSize(grid);
}

std::string DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridType(
    const int grid) {
    return state_->model_->GetGridType(grid);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridShape(const int grid,
                                                           int* shape) {
    state_->model_->GetGridShape(grid, shape);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridSpacing(const int grid,
                                                             double* spacing) {
    state_->model_->GetGridSpacing(grid, spacing);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridOrigin(const int grid,
                                                            double* origin) {
    state_->model_->GetGridOrigin(grid, origin);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridX(const int grid,
                                                       double* x) {
    state_->model_->GetGridX(grid, x);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridY(const int grid,
                                                       double* y) {
    state_->model_->GetGridY(grid, y);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridZ(const int grid,
                                                       double* z) {
    state_->model_->GetGridZ(grid, z);
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridNodeCount(const int grid) {
    return state_->model_->GetGridNodeCount(grid);
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridEdgeCount(const int grid) {
    return state_->model_->GetGridEdgeCount(grid);
}

int DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridFaceCount(const int grid) {
    return state_->model_->GetGridFaceCount(grid);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridEdgeNodes(
    const int grid,
    int* edge_nodes) {
    state_->model_->GetGridEdgeNodes(grid, edge_nodes);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridFaceEdges(
    const int grid,
    int* face_edges) {
    state_->model_->GetGridFaceEdges(grid, face_edges);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridFaceNodes(
    const int grid,
    int* face_nodes) {
    state_->model_->GetGridFaceNodes(grid, face_nodes);
}

void DynamicBmiModel<DynamicBmiLanguage::cpp>::GetGridNodesPerFace(
    const int grid,
    int* nodes_per_face) {
    state_->model_->GetGridNodesPerFace(grid, nodes_per_face);
}

struct DynamicBmiModel<DynamicBmiLanguage::fortran>::State {
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

DynamicBmiModel<DynamicBmiLanguage::fortran>::DynamicBmiModel(
    std::string plugin_path)
    : state_(std::make_unique<State>(std::move(plugin_path))) {
}

DynamicBmiModel<DynamicBmiLanguage::fortran>::~DynamicBmiModel() = default;

void DynamicBmiModel<DynamicBmiLanguage::fortran>::Initialize(
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

void DynamicBmiModel<DynamicBmiLanguage::fortran>::Update() {
    check_bmi_status(update(&state_->model_handle_), "update");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::UpdateUntil(double time) {
    check_bmi_status(update_until(&state_->model_handle_, &time), "update_until");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::Finalize() {
    state_->finalize_model();
    state_->unregister_model();
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetComponentName() {
    BmiStringBuffer name{};
    check_bmi_status(get_component_name(&state_->model_handle_, name.data()),
                     "get_component_name");
    return name.data();
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetInputItemCount() {
    int count = 0;
    check_bmi_status(get_input_item_count(&state_->model_handle_, &count),
                     "get_input_item_count");
    return count;
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetOutputItemCount() {
    int count = 0;
    check_bmi_status(get_output_item_count(&state_->model_handle_, &count),
                     "get_output_item_count");
    return count;
}

std::vector<std::string>
DynamicBmiModel<DynamicBmiLanguage::fortran>::GetInputVarNames() {
    return read_var_names(&state_->model_handle_,
                          GetInputItemCount(),
                          get_input_var_names,
                          "get_input_var_names");
}

std::vector<std::string>
DynamicBmiModel<DynamicBmiLanguage::fortran>::GetOutputVarNames() {
    return read_var_names(&state_->model_handle_,
                          GetOutputItemCount(),
                          get_output_var_names,
                          "get_output_var_names");
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarGrid(std::string name) {
    int grid = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_grid(&state_->model_handle_, name_buffer.data(), &grid),
        "get_var_grid");
    return grid;
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarType(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer type{};
    check_bmi_status(
        get_var_type(&state_->model_handle_, name_buffer.data(), type.data()),
        "get_var_type");
    return normalize_var_type(type.data());
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarUnits(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer units{};
    check_bmi_status(
        get_var_units(&state_->model_handle_, name_buffer.data(), units.data()),
        "get_var_units");
    return units.data();
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarItemsize(
    std::string name) {
    int size = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_itemsize(&state_->model_handle_, name_buffer.data(), &size),
        "get_var_itemsize");
    return size;
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarNbytes(
    std::string name) {
    int nbytes = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_nbytes(&state_->model_handle_, name_buffer.data(), &nbytes),
        "get_var_nbytes");
    return nbytes;
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetVarLocation(
    std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer location{};
    check_bmi_status(
        get_var_location(
            &state_->model_handle_, name_buffer.data(), location.data()),
        "get_var_location");
    return location.data();
}

double DynamicBmiModel<DynamicBmiLanguage::fortran>::GetCurrentTime() {
    double time = 0.0;
    check_bmi_status(get_current_time(&state_->model_handle_, &time),
                     "get_current_time");
    return time;
}

double DynamicBmiModel<DynamicBmiLanguage::fortran>::GetStartTime() {
    double time = 0.0;
    check_bmi_status(get_start_time(&state_->model_handle_, &time),
                     "get_start_time");
    return time;
}

double DynamicBmiModel<DynamicBmiLanguage::fortran>::GetEndTime() {
    double time = 0.0;
    check_bmi_status(get_end_time(&state_->model_handle_, &time),
                     "get_end_time");
    return time;
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetTimeUnits() {
    BmiStringBuffer units{};
    check_bmi_status(get_time_units(&state_->model_handle_, units.data()),
                     "get_time_units");
    return units.data();
}

double DynamicBmiModel<DynamicBmiLanguage::fortran>::GetTimeStep() {
    double time_step = 0.0;
    check_bmi_status(get_time_step(&state_->model_handle_, &time_step),
                     "get_time_step");
    return time_step;
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetValue(std::string name,
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

void* DynamicBmiModel<DynamicBmiLanguage::fortran>::GetValuePtr(
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

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetValueAtIndices(
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

void DynamicBmiModel<DynamicBmiLanguage::fortran>::SetValue(std::string name,
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

void DynamicBmiModel<DynamicBmiLanguage::fortran>::SetValueAtIndices(
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

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridRank(const int grid) {
    int mutable_grid = grid;
    int rank = 0;
    check_bmi_status(get_grid_rank(&state_->model_handle_, &mutable_grid, &rank),
                     "get_grid_rank");
    return rank;
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridSize(const int grid) {
    int mutable_grid = grid;
    int size = 0;
    check_bmi_status(get_grid_size(&state_->model_handle_, &mutable_grid, &size),
                     "get_grid_size");
    return size;
}

std::string DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridType(
    const int grid) {
    int mutable_grid = grid;
    BmiStringBuffer type{};
    check_bmi_status(
        get_grid_type(&state_->model_handle_, &mutable_grid, type.data()),
        "get_grid_type");
    return type.data();
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridShape(
    const int grid,
    int* shape) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_shape(&state_->model_handle_, &mutable_grid, shape),
        "get_grid_shape");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridSpacing(
    const int grid,
    double* spacing) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_spacing(&state_->model_handle_, &mutable_grid, spacing),
        "get_grid_spacing");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridOrigin(
    const int grid,
    double* origin) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_origin(&state_->model_handle_, &mutable_grid, origin),
        "get_grid_origin");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridX(const int grid,
                                                           double* x) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_x(&state_->model_handle_, &mutable_grid, x),
                     "get_grid_x");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridY(const int grid,
                                                           double* y) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_y(&state_->model_handle_, &mutable_grid, y),
                     "get_grid_y");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridZ(const int grid,
                                                           double* z) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_z(&state_->model_handle_, &mutable_grid, z),
                     "get_grid_z");
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridNodeCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_node_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_node_count");
    return count;
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridEdgeCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_edge_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_edge_count");
    return count;
}

int DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridFaceCount(
    const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(
        get_grid_face_count(&state_->model_handle_, &mutable_grid, &count),
        "get_grid_face_count");
    return count;
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridEdgeNodes(
    const int grid,
    int* edge_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_edge_nodes(&state_->model_handle_, &mutable_grid, edge_nodes),
        "get_grid_edge_nodes");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridFaceEdges(
    const int grid,
    int* face_edges) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_edges(&state_->model_handle_, &mutable_grid, face_edges),
        "get_grid_face_edges");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridFaceNodes(
    const int grid,
    int* face_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_nodes(&state_->model_handle_, &mutable_grid, face_nodes),
        "get_grid_face_nodes");
}

void DynamicBmiModel<DynamicBmiLanguage::fortran>::GetGridNodesPerFace(
    const int grid,
    int* nodes_per_face) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_nodes_per_face(
            &state_->model_handle_, &mutable_grid, nodes_per_face),
        "get_grid_nodes_per_face");
}

} // namespace miaow
