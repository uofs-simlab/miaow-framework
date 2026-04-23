#include "bmi/internal.hpp"

#include <filesystem>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace miaow::bmi {

namespace {

// Write a row of output values for a given variable at a specific time, handling different 
// BMI variable types with a templated approach
template <typename T>
void write_var_row(
    ::bmi::Bmi& model,
    const std::string& name,
    std::size_t n_items,
    double time,
    std::ofstream& output_stream) {
    std::vector<T> buffer(n_items);
    model.GetValue(name, buffer.data());
    output_stream
        << std::setprecision(std::numeric_limits<double>::max_digits10) << time;
    for (const T& value : buffer) {
        output_stream << ",";
        if constexpr (std::is_floating_point_v<T>) {
            output_stream << std::setprecision(std::numeric_limits<T>::max_digits10);
        }
        output_stream << value;
    }
    output_stream << "\n";
}

} // namespace

std::vector<OutputVariable> open_output_vars(
    ::bmi::Bmi& model,
    const std::string& output_dir) {
    const std::vector<std::string> var_names = model.GetOutputVarNames();
    std::vector<OutputVariable> output_vars;
    output_vars.reserve(var_names.size());
    for (const std::string& name : var_names) {
        const std::size_t n_items = static_cast<std::size_t>(
            model.GetVarNbytes(name) / model.GetVarItemsize(name));

        std::ofstream stream(std::filesystem::path(output_dir) / (name + ".csv"));
        stream << "time";
        for (std::size_t i = 0; i < n_items; ++i) {
            stream << ",val_" << i;
        }
        stream << "\n";
        output_vars.push_back(OutputVariable{name, n_items, std::move(stream)});
    }
    return output_vars;
}

void write_output_step(::bmi::Bmi& model, OutputVariable& variable, double time) {
    const std::string type = model.GetVarType(variable.name_);
    if (type == "double") {
        write_var_row<double>(
            model, variable.name_, variable.n_items_, time, variable.stream_);
    } else if (type == "float") {
        write_var_row<float>(
            model, variable.name_, variable.n_items_, time, variable.stream_);
    } else if (type == "int") {
        write_var_row<int>(
            model, variable.name_, variable.n_items_, time, variable.stream_);
    } else {
        throw std::runtime_error(
            "unsupported BMI variable type '" + type + "' for variable '" +
            variable.name_ + "'");
    }
}

} // namespace miaow::bmi
