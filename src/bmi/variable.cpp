#include "bmi/internal.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace miaow::bmi {

namespace {

// Convert a BMI type name to the normalized scalar representation used by MIAOW
BmiValueType resolve_bmi_value_type(const std::string& type_name,
                                    const std::string& variable_name,
                                    std::string_view error_prefix) {
    if (type_name == "float") {
        return BmiValueType::bmi_float;
    }
    if (type_name == "double") {
        return BmiValueType::bmi_double;
    }
    if (type_name == "int") {
        return BmiValueType::bmi_int;
    }

    throw std::runtime_error(std::string(error_prefix) + "variable '" + variable_name +
                             "' has unsupported BMI type '" + type_name + "'");
}

// Identify the numeric types accepted by direct coupling
bool is_coupling_type(BmiValueType type) {
    return type == BmiValueType::bmi_float || type == BmiValueType::bmi_double;
}

// Transfer one complete variable, converting types to the target variable's datatype
template <typename Source, typename Target>
void transfer_values(::bmi::Bmi& source_model, const BmiVariable& source_variable,
                     ::bmi::Bmi& target_model, const BmiVariable& target_variable) {
    std::vector<Source> source_values(source_variable.item_count());
    source_model.GetValue(source_variable.name(), source_values.data());

    // Reuse the source buffer when both endpoints have the same element type
    if constexpr (std::is_same_v<Source, Target>) {
        target_model.SetValue(target_variable.name(), source_values.data());
    } else {
        // Otherwise convert every source item to the target element type before transfer
        std::vector<Target> target_values;
        target_values.reserve(source_values.size());
        for (const Source value : source_values) {
            target_values.push_back(static_cast<Target>(value));
        }
        target_model.SetValue(target_variable.name(), target_values.data());
    }
}

} // namespace

// Return the canonical BMI name for one normalized scalar type
std::string_view bmi_value_type_name(BmiValueType type) {
    switch (type) {
    case BmiValueType::bmi_float:
        return "float";
    case BmiValueType::bmi_double:
        return "double";
    case BmiValueType::bmi_int:
        return "int";
    }

    throw std::logic_error("unknown BmiValueType");
}

// Return the native C++ scalar size for one normalized BMI type
std::size_t bmi_value_type_size(BmiValueType type) {
    switch (type) {
    case BmiValueType::bmi_float:
        return sizeof(float);
    case BmiValueType::bmi_double:
        return sizeof(double);
    case BmiValueType::bmi_int:
        return sizeof(int);
    }

    throw std::logic_error("unknown BmiValueType");
}

// Construct a metadata snapshot from explicit values
BmiVariable::BmiVariable(std::string name, BmiValueType type, std::string units,
                         std::string location, int grid_id, std::size_t item_count)
    : name_(std::move(name)), type_(type), units_(std::move(units)),
      location_(std::move(location)), grid_id_(grid_id), item_count_(item_count) {}

// Construct and validate a metadata snapshot from an initialized BMI model
BmiVariable::BmiVariable(::bmi::Bmi& model, const std::string& name,
                         std::string_view error_prefix)
    : name_(name) {
    // Resolve the scalar representation before querying remaining metadata
    const std::string type_name = model.GetVarType(name);
    type_ = resolve_bmi_value_type(type_name, name, error_prefix);
    const int item_size = model.GetVarItemsize(name);
    const int byte_count = model.GetVarNbytes(name);

    // Validate the scalar representation before calculating the flattened count
    const std::size_t expected_item_size = bmi_value_type_size(type_);
    if (item_size != static_cast<int>(expected_item_size)) {
        throw std::runtime_error(std::string(error_prefix) + "variable '" + name +
                                 "' reports item size " + std::to_string(item_size) +
                                 " for BMI type '" + type_name + "'; expected " +
                                 std::to_string(expected_item_size));
    }
    if (byte_count <= 0) {
        throw std::runtime_error(std::string(error_prefix) + "variable '" + name +
                                 "' reports a non-positive byte count " +
                                 std::to_string(byte_count));
    }
    if (byte_count % item_size != 0) {
        throw std::runtime_error(std::string(error_prefix) + "variable '" + name +
                                 "' reports byte count " + std::to_string(byte_count) +
                                 " that is not divisible by item size " +
                                 std::to_string(item_size));
    }

    // Complete the immutable metadata snapshot after size validation
    units_ = model.GetVarUnits(name);
    location_ = model.GetVarLocation(name);
    grid_id_ = model.GetVarGrid(name);
    item_count_ = static_cast<std::size_t>(byte_count / item_size);
}

// Expose cached metadata without allowing mutation
const std::string& BmiVariable::name() const {
    return name_;
}

BmiValueType BmiVariable::type() const {
    return type_;
}

const std::string& BmiVariable::units() const {
    return units_;
}

const std::string& BmiVariable::location() const {
    return location_;
}

int BmiVariable::grid_id() const {
    return grid_id_;
}

std::size_t BmiVariable::item_count() const {
    return item_count_;
}

// Construct and validate one coupling from initialized model endpoints
BmiCoupling::BmiCoupling(std::size_t source_module_index, ::bmi::Bmi& source_model,
                         const std::string& source_name,
                         std::size_t target_module_index, ::bmi::Bmi& target_model,
                         const std::string& target_name)
    : source_module_index_(source_module_index),
      source_variable_(source_model, source_name, "BMI coupling error: "),
      target_module_index_(target_module_index),
      target_variable_(target_model, target_name, "BMI coupling error: ") {
    // Restrict direct transfer to the supported floating-point conversion surface
    if (!is_coupling_type(source_variable_.type())) {
        throw std::runtime_error("BMI coupling error: source variable '" + source_name +
                                 "' must have BMI type 'double' or 'float' but has '" +
                                 std::string(bmi_value_type_name(source_variable_.type())) +
                                 "'");
    }
    if (!is_coupling_type(target_variable_.type())) {
        throw std::runtime_error("BMI coupling error: target variable '" + target_name +
                                 "' must have BMI type 'double' or 'float' but has '" +
                                 std::string(bmi_value_type_name(target_variable_.type())) +
                                 "'");
    }
    if (source_variable_.item_count() != target_variable_.item_count()) {
        throw std::runtime_error("BMI coupling error: source variable '" + source_name +
                                 "' has " + std::to_string(source_variable_.item_count()) +
                                 " item(s) but target variable '" + target_name + "' has " +
                                 std::to_string(target_variable_.item_count()) +
                                 " item(s); coupling requires equal item counts");
    }
}

// Expose module indices used by pipeline orchestration
std::size_t BmiCoupling::source_module_index() const {
    return source_module_index_;
}

std::size_t BmiCoupling::target_module_index() const {
    return target_module_index_;
}

// Apply one complete variable transfer between the stored endpoints
void BmiCoupling::apply(::bmi::Bmi& source_model, ::bmi::Bmi& target_model) const {
    // Dispatch once per complete coupling outside the flattened item loop
    if (source_variable_.type() == BmiValueType::bmi_double) {
        if (target_variable_.type() == BmiValueType::bmi_double) {
            transfer_values<double, double>(source_model, source_variable_, target_model,
                                            target_variable_);
        } else {
            transfer_values<double, float>(source_model, source_variable_, target_model,
                                           target_variable_);
        }
    } else if (target_variable_.type() == BmiValueType::bmi_double) {
        transfer_values<float, double>(source_model, source_variable_, target_model,
                                       target_variable_);
    } else {
        transfer_values<float, float>(source_model, source_variable_, target_model,
                                      target_variable_);
    }
}

} // namespace miaow::bmi
