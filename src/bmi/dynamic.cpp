#include "bmi/internal.hpp"

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

DynamicBmiModel::DynamicBmiModel(const DynamicBmiConfig& config)
    : backend_(config.backend_) {
    switch (backend_) {
    case DynamicBmiBackend::cpp:
        open_cpp_plugin(config);
        break;
    case DynamicBmiBackend::fortran:
        open_fortran_plugin(config);
        break;
    }
}

DynamicBmiModel::~DynamicBmiModel() {
    cleanup_model();
    close_dynamic_library(library_handle_);
}

// Open a C++ plugin and construct its BMI model instance
void DynamicBmiModel::open_cpp_plugin(const DynamicBmiConfig& config) {
    library_handle_ = open_dynamic_library(config.plugin_path_, cpp_plugin_error_prefix);

    try {
        void* create_symbol =
            resolve_required_dynamic_symbol(library_handle_, "bmi_model_create",
                                            cpp_plugin_error_prefix);
        const CreateBmiFunction create_bmi =
            reinterpret_cast<CreateBmiFunction>(create_symbol);

        void* destroy_symbol =
            resolve_required_dynamic_symbol(library_handle_, "bmi_model_destroy",
                                            cpp_plugin_error_prefix);
        destroy_bmi_ = reinterpret_cast<DestroyBmiFunction>(destroy_symbol);

        cpp_model_ = create_bmi();
        if (cpp_model_ == nullptr) {
            throw std::runtime_error(
                "BMI dynamic C++ plugin error: create function returned null model");
        }
    } catch (...) {
        cleanup_model();
        close_dynamic_library(library_handle_);
        throw;
    }
}

// Open a Fortran plugin and resolve the procedural BMI bridge symbols
void DynamicBmiModel::open_fortran_plugin(const DynamicBmiConfig& config) {
    library_handle_ =
        open_dynamic_library(config.plugin_path_, fortran_plugin_error_prefix);
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

void DynamicBmiModel::unregister_fortran_model() {
    if (fortran_model_handle_ == nullptr) {
        return;
    }
    if (unregister_bmi_ != nullptr) {
        (void)unregister_bmi_(&fortran_model_handle_);
    }
    fortran_model_handle_ = nullptr;
}

// Release the backend model before its dynamic library is closed
void DynamicBmiModel::cleanup_model() {
    if (backend_ == DynamicBmiBackend::cpp) {
        if (cpp_model_ == nullptr) {
            return;
        }
        destroy_bmi_(cpp_model_);
        cpp_model_ = nullptr;
        return;
    }

    unregister_fortran_model();
}

void DynamicBmiModel::Initialize(std::string config_file) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->Initialize(std::move(config_file));
        return;
    }

    if (fortran_model_handle_ == nullptr) {
        check_bmi_status(register_bmi_(&fortran_model_handle_), "register_bmi");
    }

    BmiStringBuffer config_file_buffer = make_bmi_string(config_file, "BMI config path");
    check_bmi_status(initialize(&fortran_model_handle_, config_file_buffer.data()),
                     "initialize");
}

void DynamicBmiModel::Update() {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->Update();
        return;
    }
    check_bmi_status(update(&fortran_model_handle_), "update");
}

void DynamicBmiModel::UpdateUntil(double time) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->UpdateUntil(time);
        return;
    }
    check_bmi_status(update_until(&fortran_model_handle_, &time), "update_until");
}

void DynamicBmiModel::Finalize() {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->Finalize();
        return;
    }

    // The procedural bridge frees the model and its handle even on finalization failure
    const int status = finalize(&fortran_model_handle_);
    fortran_model_handle_ = nullptr;
    check_bmi_status(status, "finalize");
}

std::string DynamicBmiModel::GetComponentName() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetComponentName();
    }
    BmiStringBuffer name{};
    check_bmi_status(get_component_name(&fortran_model_handle_, name.data()),
                     "get_component_name");
    return name.data();
}

int DynamicBmiModel::GetInputItemCount() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetInputItemCount();
    }
    int count = 0;
    check_bmi_status(get_input_item_count(&fortran_model_handle_, &count),
                     "get_input_item_count");
    return count;
}

int DynamicBmiModel::GetOutputItemCount() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetOutputItemCount();
    }
    int count = 0;
    check_bmi_status(get_output_item_count(&fortran_model_handle_, &count),
                     "get_output_item_count");
    return count;
}

std::vector<std::string> DynamicBmiModel::GetInputVarNames() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetInputVarNames();
    }
    return read_var_names(&fortran_model_handle_, GetInputItemCount(), get_input_var_names,
                          "get_input_var_names");
}

std::vector<std::string> DynamicBmiModel::GetOutputVarNames() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetOutputVarNames();
    }
    return read_var_names(&fortran_model_handle_, GetOutputItemCount(),
                          get_output_var_names, "get_output_var_names");
}

int DynamicBmiModel::GetVarGrid(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarGrid(std::move(name));
    }
    int grid = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_grid(&fortran_model_handle_, name_buffer.data(), &grid),
                     "get_var_grid");
    return grid;
}

std::string DynamicBmiModel::GetVarType(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarType(std::move(name));
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer type{};
    check_bmi_status(get_var_type(&fortran_model_handle_, name_buffer.data(), type.data()),
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

std::string DynamicBmiModel::GetVarUnits(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarUnits(std::move(name));
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer units{};
    check_bmi_status(get_var_units(&fortran_model_handle_, name_buffer.data(),
                                   units.data()),
                     "get_var_units");
    return units.data();
}

int DynamicBmiModel::GetVarItemsize(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarItemsize(std::move(name));
    }
    int size = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_itemsize(&fortran_model_handle_, name_buffer.data(), &size),
                     "get_var_itemsize");
    return size;
}

int DynamicBmiModel::GetVarNbytes(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarNbytes(std::move(name));
    }
    int nbytes = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_nbytes(&fortran_model_handle_, name_buffer.data(), &nbytes),
                     "get_var_nbytes");
    return nbytes;
}

std::string DynamicBmiModel::GetVarLocation(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetVarLocation(std::move(name));
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer location{};
    check_bmi_status(get_var_location(&fortran_model_handle_, name_buffer.data(),
                                      location.data()),
                     "get_var_location");
    return location.data();
}

double DynamicBmiModel::GetCurrentTime() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetCurrentTime();
    }
    double time = 0.0;
    check_bmi_status(get_current_time(&fortran_model_handle_, &time), "get_current_time");
    return time;
}

double DynamicBmiModel::GetStartTime() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetStartTime();
    }
    double time = 0.0;
    check_bmi_status(get_start_time(&fortran_model_handle_, &time), "get_start_time");
    return time;
}

double DynamicBmiModel::GetEndTime() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetEndTime();
    }
    double time = 0.0;
    check_bmi_status(get_end_time(&fortran_model_handle_, &time), "get_end_time");
    return time;
}

std::string DynamicBmiModel::GetTimeUnits() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetTimeUnits();
    }
    BmiStringBuffer units{};
    check_bmi_status(get_time_units(&fortran_model_handle_, units.data()),
                     "get_time_units");
    return units.data();
}

double DynamicBmiModel::GetTimeStep() {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetTimeStep();
    }
    double time_step = 0.0;
    check_bmi_status(get_time_step(&fortran_model_handle_, &time_step), "get_time_step");
    return time_step;
}

void DynamicBmiModel::GetValue(std::string name, void* dest) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetValue(std::move(name), dest);
        return;
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(get_value_int(&fortran_model_handle_, name_buffer.data(),
                                       static_cast<int*>(dest)),
                         "get_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(get_value_float(&fortran_model_handle_, name_buffer.data(),
                                         static_cast<float*>(dest)),
                         "get_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(get_value_double(&fortran_model_handle_, name_buffer.data(),
                                          static_cast<double*>(dest)),
                         "get_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void* DynamicBmiModel::GetValuePtr(std::string name) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetValuePtr(std::move(name));
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    void* value_ptr = nullptr;
    if (type == "int") {
        check_bmi_status(get_value_ptr_int(&fortran_model_handle_, name_buffer.data(),
                                           &value_ptr),
                         "get_value_ptr_int");
        return value_ptr;
    }
    if (type == "float") {
        check_bmi_status(get_value_ptr_float(&fortran_model_handle_, name_buffer.data(),
                                             &value_ptr),
                         "get_value_ptr_float");
        return value_ptr;
    }
    if (type == "double") {
        check_bmi_status(get_value_ptr_double(&fortran_model_handle_, name_buffer.data(),
                                              &value_ptr),
                         "get_value_ptr_double");
        return value_ptr;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::GetValueAtIndices(std::string name, void* dest, int* inds,
                                        int count) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetValueAtIndices(std::move(name), dest, inds, count);
        return;
    }
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(get_value_at_indices_int(&fortran_model_handle_,
                                                  name_buffer.data(),
                                                  static_cast<int*>(dest), inds),
                         "get_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(get_value_at_indices_float(&fortran_model_handle_,
                                                    name_buffer.data(),
                                                    static_cast<float*>(dest), inds),
                         "get_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(get_value_at_indices_double(&fortran_model_handle_,
                                                     name_buffer.data(),
                                                     static_cast<double*>(dest), inds),
                         "get_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::SetValue(std::string name, void* src) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->SetValue(std::move(name), src);
        return;
    }
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(set_value_int(&fortran_model_handle_, name_buffer.data(),
                                       static_cast<int*>(src)),
                         "set_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(set_value_float(&fortran_model_handle_, name_buffer.data(),
                                         static_cast<float*>(src)),
                         "set_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(set_value_double(&fortran_model_handle_, name_buffer.data(),
                                          static_cast<double*>(src)),
                         "set_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::SetValueAtIndices(std::string name, int* inds, int count, void* src) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->SetValueAtIndices(std::move(name), inds, count, src);
        return;
    }
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(set_value_at_indices_int(&fortran_model_handle_,
                                                  name_buffer.data(), inds,
                                                  static_cast<int*>(src)),
                         "set_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(set_value_at_indices_float(&fortran_model_handle_,
                                                    name_buffer.data(), inds,
                                                    static_cast<float*>(src)),
                         "set_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(set_value_at_indices_double(&fortran_model_handle_,
                                                     name_buffer.data(), inds,
                                                     static_cast<double*>(src)),
                         "set_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

int DynamicBmiModel::GetGridRank(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridRank(grid);
    }
    int mutable_grid = grid;
    int rank = 0;
    check_bmi_status(get_grid_rank(&fortran_model_handle_, &mutable_grid, &rank),
                     "get_grid_rank");
    return rank;
}

int DynamicBmiModel::GetGridSize(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridSize(grid);
    }
    int mutable_grid = grid;
    int size = 0;
    check_bmi_status(get_grid_size(&fortran_model_handle_, &mutable_grid, &size),
                     "get_grid_size");
    return size;
}

std::string DynamicBmiModel::GetGridType(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridType(grid);
    }
    int mutable_grid = grid;
    BmiStringBuffer type{};
    check_bmi_status(get_grid_type(&fortran_model_handle_, &mutable_grid, type.data()),
                     "get_grid_type");
    return type.data();
}

void DynamicBmiModel::GetGridShape(const int grid, int* shape) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridShape(grid, shape);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_shape(&fortran_model_handle_, &mutable_grid, shape),
                     "get_grid_shape");
}

void DynamicBmiModel::GetGridSpacing(const int grid, double* spacing) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridSpacing(grid, spacing);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_spacing(&fortran_model_handle_, &mutable_grid, spacing),
                     "get_grid_spacing");
}

void DynamicBmiModel::GetGridOrigin(const int grid, double* origin) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridOrigin(grid, origin);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_origin(&fortran_model_handle_, &mutable_grid, origin),
                     "get_grid_origin");
}

void DynamicBmiModel::GetGridX(const int grid, double* x) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridX(grid, x);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_x(&fortran_model_handle_, &mutable_grid, x), "get_grid_x");
}

void DynamicBmiModel::GetGridY(const int grid, double* y) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridY(grid, y);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_y(&fortran_model_handle_, &mutable_grid, y), "get_grid_y");
}

void DynamicBmiModel::GetGridZ(const int grid, double* z) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridZ(grid, z);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_z(&fortran_model_handle_, &mutable_grid, z), "get_grid_z");
}

int DynamicBmiModel::GetGridNodeCount(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridNodeCount(grid);
    }
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_node_count(&fortran_model_handle_, &mutable_grid, &count),
                     "get_grid_node_count");
    return count;
}

int DynamicBmiModel::GetGridEdgeCount(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridEdgeCount(grid);
    }
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_edge_count(&fortran_model_handle_, &mutable_grid, &count),
                     "get_grid_edge_count");
    return count;
}

int DynamicBmiModel::GetGridFaceCount(const int grid) {
    if (backend_ == DynamicBmiBackend::cpp) {
        return cpp_model_->GetGridFaceCount(grid);
    }
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_face_count(&fortran_model_handle_, &mutable_grid, &count),
                     "get_grid_face_count");
    return count;
}

void DynamicBmiModel::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridEdgeNodes(grid, edge_nodes);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_edge_nodes(&fortran_model_handle_, &mutable_grid, edge_nodes),
                     "get_grid_edge_nodes");
}

void DynamicBmiModel::GetGridFaceEdges(const int grid, int* face_edges) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridFaceEdges(grid, face_edges);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_face_edges(&fortran_model_handle_, &mutable_grid, face_edges),
                     "get_grid_face_edges");
}

void DynamicBmiModel::GetGridFaceNodes(const int grid, int* face_nodes) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridFaceNodes(grid, face_nodes);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_face_nodes(&fortran_model_handle_, &mutable_grid, face_nodes),
                     "get_grid_face_nodes");
}

void DynamicBmiModel::GetGridNodesPerFace(const int grid, int* nodes_per_face) {
    if (backend_ == DynamicBmiBackend::cpp) {
        cpp_model_->GetGridNodesPerFace(grid, nodes_per_face);
        return;
    }
    int mutable_grid = grid;
    check_bmi_status(get_grid_nodes_per_face(&fortran_model_handle_, &mutable_grid,
                                             nodes_per_face),
                     "get_grid_nodes_per_face");
}

} // namespace miaow
