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

// Write output values for one BMI variable into the current CSV row
template <typename T>
void write_var_values(
    ::bmi::Bmi& model,
    const std::string& name,
    std::size_t n_items,
    std::ofstream& output_stream) {
    std::vector<T> buffer(n_items);
    model.GetValue(name, buffer.data());
    for (const T& value : buffer) {
        output_stream << ",";
        if constexpr (std::is_floating_point_v<T>) {
            output_stream << std::setprecision(std::numeric_limits<T>::max_digits10);
        }
        output_stream << value;
    }
}

} // namespace

OutputFile open_output_file(
    ::bmi::Bmi& model,
    const std::string& output_dir,
    const std::string& output_name) {
    if (output_name.empty()) {
        throw std::runtime_error("BMI output error: task output name is empty");
    }

    const std::filesystem::path output_directory(output_dir);
    const std::filesystem::path output_filename = output_name + ".csv";
    const std::filesystem::path output_path = output_directory / output_filename;

    const std::vector<std::string> var_names = model.GetOutputVarNames();
    OutputFile output_file;
    output_file.artifact_path_ =
        (output_directory.filename() / output_filename).string();
    output_file.variables_.reserve(var_names.size());
    output_file.stream_.open(output_path);
    if (!output_file.stream_.is_open()) {
        throw std::runtime_error("unable to open BMI output file '" +
                                 output_path.string() + "'");
    }

    output_file.stream_ << "time";
    for (const std::string& name : var_names) {
        const std::size_t n_items = static_cast<std::size_t>(
            model.GetVarNbytes(name) / model.GetVarItemsize(name));

        if (n_items == 1) {
            output_file.stream_ << "," << name;
        } else {
            for (std::size_t i = 0; i < n_items; ++i) {
                output_file.stream_ << "," << name << "_" << i;
            }
        }
        output_file.variables_.push_back(OutputVariable{name, n_items});
    }
    output_file.stream_ << "\n";
    return output_file;
}

void write_output_step(::bmi::Bmi& model, OutputFile& output_file, double time) {
    output_file.stream_
        << std::setprecision(std::numeric_limits<double>::max_digits10) << time;
    for (const OutputVariable& variable : output_file.variables_) {
        const std::string type = model.GetVarType(variable.name_);
        if (type == "double") {
            write_var_values<double>(
                model, variable.name_, variable.n_items_, output_file.stream_);
        } else if (type == "float") {
            write_var_values<float>(
                model, variable.name_, variable.n_items_, output_file.stream_);
        } else if (type == "int") {
            write_var_values<int>(
                model, variable.name_, variable.n_items_, output_file.stream_);
        } else {
            throw std::runtime_error(
                "unsupported BMI variable type '" + type + "' for variable '" +
                variable.name_ + "'");
        }
    }
    output_file.stream_ << "\n";
}

} // namespace miaow::bmi
