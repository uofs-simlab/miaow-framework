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

void* resolve_symbol(void* plugin_handle, const std::string& symbol_name) {
    // Clear any previous loader error before looking up the fixed plugin symbol
    dlerror();
    void* symbol = dlsym(plugin_handle, symbol_name.c_str());
    const char* error = dlerror();
    if (error != nullptr) {
        throw std::runtime_error(
            "unable to resolve BMI plugin symbol '" + symbol_name + "': " +
            error);
    }
    return symbol;
}

} // namespace

DynamicBmiModel::DynamicBmiModel(std::string plugin_path)
    : plugin_path_(std::move(plugin_path)) {
    if (plugin_path_.empty()) {
        throw std::runtime_error("BMI plugin path is empty");
    }

    // RTLD_LOCAL keeps each model's register_bmi symbol scoped to its plugin handle
    plugin_handle_ = dlopen(plugin_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (plugin_handle_ == nullptr) {
        const char* error = dlerror();
        const std::string detail = error == nullptr ? "unknown loader error" : error;
        throw std::runtime_error(
            "unable to load BMI plugin '" + plugin_path_ + "': " + detail);
    }

    // POSIX dlsym returns an object pointer that represents a function address
    void* symbol = resolve_symbol(plugin_handle_, "register_bmi");
    register_bmi_ = reinterpret_cast<RegisterBmiFunction>(symbol);
}

DynamicBmiModel::~DynamicBmiModel() {
    if (model_handle_ != nullptr) {
        (void)finalize(&model_handle_);
        model_handle_ = nullptr;
    }
    if (plugin_handle_ != nullptr) {
        (void)dlclose(plugin_handle_);
        plugin_handle_ = nullptr;
    }
}

void DynamicBmiModel::Initialize(std::string config_file) {
    if (model_handle_ == nullptr) {
        check_bmi_status(register_bmi_(&model_handle_), "register_bmi");
    }

    BmiStringBuffer config_file_buffer =
        make_bmi_string(config_file, "BMI config path");
    check_bmi_status(initialize(&model_handle_, config_file_buffer.data()),
                     "initialize");
}

void DynamicBmiModel::Update() {
    check_bmi_status(update(&model_handle_), "update");
}

void DynamicBmiModel::UpdateUntil(double time) {
    check_bmi_status(update_until(&model_handle_, &time), "update_until");
}

void DynamicBmiModel::Finalize() {
    check_bmi_status(finalize(&model_handle_), "finalize");
    model_handle_ = nullptr;
}

std::string DynamicBmiModel::GetComponentName() {
    BmiStringBuffer name{};
    check_bmi_status(get_component_name(&model_handle_, name.data()),
                     "get_component_name");
    return name.data();
}

int DynamicBmiModel::GetInputItemCount() {
    int count = 0;
    check_bmi_status(get_input_item_count(&model_handle_, &count),
                     "get_input_item_count");
    return count;
}

int DynamicBmiModel::GetOutputItemCount() {
    int count = 0;
    check_bmi_status(get_output_item_count(&model_handle_, &count),
                     "get_output_item_count");
    return count;
}

std::vector<std::string> DynamicBmiModel::GetInputVarNames() {
    return read_var_names(&model_handle_,
                          GetInputItemCount(),
                          get_input_var_names,
                          "get_input_var_names");
}

std::vector<std::string> DynamicBmiModel::GetOutputVarNames() {
    return read_var_names(&model_handle_,
                          GetOutputItemCount(),
                          get_output_var_names,
                          "get_output_var_names");
}

int DynamicBmiModel::GetVarGrid(std::string name) {
    int grid = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(get_var_grid(&model_handle_, name_buffer.data(), &grid),
                     "get_var_grid");
    return grid;
}

std::string DynamicBmiModel::GetVarType(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer type{};
    check_bmi_status(get_var_type(&model_handle_, name_buffer.data(), type.data()),
                     "get_var_type");
    return normalize_var_type(type.data());
}

std::string DynamicBmiModel::GetVarUnits(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer units{};
    check_bmi_status(get_var_units(&model_handle_, name_buffer.data(), units.data()),
                     "get_var_units");
    return units.data();
}

int DynamicBmiModel::GetVarItemsize(std::string name) {
    int size = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_itemsize(&model_handle_, name_buffer.data(), &size),
        "get_var_itemsize");
    return size;
}

int DynamicBmiModel::GetVarNbytes(std::string name) {
    int nbytes = 0;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    check_bmi_status(
        get_var_nbytes(&model_handle_, name_buffer.data(), &nbytes),
        "get_var_nbytes");
    return nbytes;
}

std::string DynamicBmiModel::GetVarLocation(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    BmiStringBuffer location{};
    check_bmi_status(
        get_var_location(&model_handle_, name_buffer.data(), location.data()),
        "get_var_location");
    return location.data();
}

double DynamicBmiModel::GetCurrentTime() {
    double time = 0.0;
    check_bmi_status(get_current_time(&model_handle_, &time), "get_current_time");
    return time;
}

double DynamicBmiModel::GetStartTime() {
    double time = 0.0;
    check_bmi_status(get_start_time(&model_handle_, &time), "get_start_time");
    return time;
}

double DynamicBmiModel::GetEndTime() {
    double time = 0.0;
    check_bmi_status(get_end_time(&model_handle_, &time), "get_end_time");
    return time;
}

std::string DynamicBmiModel::GetTimeUnits() {
    BmiStringBuffer units{};
    check_bmi_status(get_time_units(&model_handle_, units.data()), "get_time_units");
    return units.data();
}

double DynamicBmiModel::GetTimeStep() {
    double time_step = 0.0;
    check_bmi_status(get_time_step(&model_handle_, &time_step), "get_time_step");
    return time_step;
}

void DynamicBmiModel::GetValue(std::string name, void* dest) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            get_value_int(&model_handle_, name_buffer.data(), static_cast<int*>(dest)),
            "get_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_float(
                &model_handle_, name_buffer.data(), static_cast<float*>(dest)),
            "get_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_double(
                &model_handle_, name_buffer.data(), static_cast<double*>(dest)),
            "get_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void* DynamicBmiModel::GetValuePtr(std::string name) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    void* value_ptr = nullptr;
    if (type == "int") {
        check_bmi_status(
            get_value_ptr_int(&model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_int");
        return value_ptr;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_ptr_float(&model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_float");
        return value_ptr;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_ptr_double(&model_handle_, name_buffer.data(), &value_ptr),
            "get_value_ptr_double");
        return value_ptr;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::GetValueAtIndices(std::string name,
                                        void* dest,
                                        int* inds,
                                        int count) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            get_value_at_indices_int(
                &model_handle_, name_buffer.data(), static_cast<int*>(dest), inds),
            "get_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            get_value_at_indices_float(
                &model_handle_, name_buffer.data(), static_cast<float*>(dest), inds),
            "get_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            get_value_at_indices_double(
                &model_handle_, name_buffer.data(), static_cast<double*>(dest), inds),
            "get_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::SetValue(std::string name, void* src) {
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            set_value_int(&model_handle_, name_buffer.data(), static_cast<int*>(src)),
            "set_value_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            set_value_float(
                &model_handle_, name_buffer.data(), static_cast<float*>(src)),
            "set_value_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            set_value_double(
                &model_handle_, name_buffer.data(), static_cast<double*>(src)),
            "set_value_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

void DynamicBmiModel::SetValueAtIndices(std::string name,
                                        int* inds,
                                        int count,
                                        void* src) {
    (void)count;
    BmiStringBuffer name_buffer = make_bmi_string(name, "BMI variable name");
    const std::string type = GetVarType(name);
    if (type == "int") {
        check_bmi_status(
            set_value_at_indices_int(
                &model_handle_, name_buffer.data(), inds, static_cast<int*>(src)),
            "set_value_at_indices_int");
        return;
    }
    if (type == "float") {
        check_bmi_status(
            set_value_at_indices_float(
                &model_handle_, name_buffer.data(), inds, static_cast<float*>(src)),
            "set_value_at_indices_float");
        return;
    }
    if (type == "double") {
        check_bmi_status(
            set_value_at_indices_double(
                &model_handle_, name_buffer.data(), inds, static_cast<double*>(src)),
            "set_value_at_indices_double");
        return;
    }
    throw std::runtime_error("unsupported BMI variable type '" + type + "'");
}

int DynamicBmiModel::GetGridRank(const int grid) {
    int mutable_grid = grid;
    int rank = 0;
    check_bmi_status(get_grid_rank(&model_handle_, &mutable_grid, &rank),
                     "get_grid_rank");
    return rank;
}

int DynamicBmiModel::GetGridSize(const int grid) {
    int mutable_grid = grid;
    int size = 0;
    check_bmi_status(get_grid_size(&model_handle_, &mutable_grid, &size),
                     "get_grid_size");
    return size;
}

std::string DynamicBmiModel::GetGridType(const int grid) {
    int mutable_grid = grid;
    BmiStringBuffer type{};
    check_bmi_status(get_grid_type(&model_handle_, &mutable_grid, type.data()),
                     "get_grid_type");
    return type.data();
}

void DynamicBmiModel::GetGridShape(const int grid, int* shape) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_shape(&model_handle_, &mutable_grid, shape),
                     "get_grid_shape");
}

void DynamicBmiModel::GetGridSpacing(const int grid, double* spacing) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_spacing(&model_handle_, &mutable_grid, spacing),
                     "get_grid_spacing");
}

void DynamicBmiModel::GetGridOrigin(const int grid, double* origin) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_origin(&model_handle_, &mutable_grid, origin),
                     "get_grid_origin");
}

void DynamicBmiModel::GetGridX(const int grid, double* x) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_x(&model_handle_, &mutable_grid, x), "get_grid_x");
}

void DynamicBmiModel::GetGridY(const int grid, double* y) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_y(&model_handle_, &mutable_grid, y), "get_grid_y");
}

void DynamicBmiModel::GetGridZ(const int grid, double* z) {
    int mutable_grid = grid;
    check_bmi_status(get_grid_z(&model_handle_, &mutable_grid, z), "get_grid_z");
}

int DynamicBmiModel::GetGridNodeCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_node_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_node_count");
    return count;
}

int DynamicBmiModel::GetGridEdgeCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_edge_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_edge_count");
    return count;
}

int DynamicBmiModel::GetGridFaceCount(const int grid) {
    int mutable_grid = grid;
    int count = 0;
    check_bmi_status(get_grid_face_count(&model_handle_, &mutable_grid, &count),
                     "get_grid_face_count");
    return count;
}

void DynamicBmiModel::GetGridEdgeNodes(const int grid, int* edge_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_edge_nodes(&model_handle_, &mutable_grid, edge_nodes),
        "get_grid_edge_nodes");
}

void DynamicBmiModel::GetGridFaceEdges(const int grid, int* face_edges) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_edges(&model_handle_, &mutable_grid, face_edges),
        "get_grid_face_edges");
}

void DynamicBmiModel::GetGridFaceNodes(const int grid, int* face_nodes) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_face_nodes(&model_handle_, &mutable_grid, face_nodes),
        "get_grid_face_nodes");
}

void DynamicBmiModel::GetGridNodesPerFace(const int grid, int* nodes_per_face) {
    int mutable_grid = grid;
    check_bmi_status(
        get_grid_nodes_per_face(&model_handle_, &mutable_grid, nodes_per_face),
        "get_grid_nodes_per_face");
}

} // namespace miaow
