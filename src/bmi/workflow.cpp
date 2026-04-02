#include "bmi/internal.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace miaow::bmi {

namespace {

struct OutputVariable {
    std::string name_;
    std::size_t n_items_;
    std::ofstream stream_;
};

std::vector<OutputVariable> open_output_vars(::bmi::Bmi& model,
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

template <typename T>
void write_var_row(::bmi::Bmi& model, const std::string& name, std::size_t n_items,
                   double time, std::ofstream& out) {
    std::vector<T> buf(n_items);
    model.GetValue(name, buf.data());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << time;
    for (const T& value : buf) {
        out << ",";
        if constexpr (std::is_floating_point_v<T>) {
            out << std::setprecision(std::numeric_limits<T>::max_digits10);
        }
        out << value;
    }
    out << "\n";
}

void write_output_step(::bmi::Bmi& model, const std::string& name,
                       std::size_t n_items, double time, std::ofstream& out) {
    const std::string type = model.GetVarType(name);
    if (type == "double") {
        write_var_row<double>(model, name, n_items, time, out);
    } else if (type == "float") {
        write_var_row<float>(model, name, n_items, time, out);
    } else if (type == "int") {
        write_var_row<int>(model, name, n_items, time, out);
    } else {
        throw std::runtime_error(
            "unsupported BMI variable type '" + type + "' for variable '" + name + "'");
    }
}

} // namespace

std::size_t run_model_updates(
    ::bmi::Bmi& model,
    const std::optional<ForcingData>& forcing_data,
    const std::string& task_output_dir) {
    std::vector<OutputVariable> output_vars;
    if (!task_output_dir.empty()) {
        output_vars = open_output_vars(model, task_output_dir);
    }

    const double end_time = model.GetEndTime();
    std::size_t forcing_step = 0;
    std::size_t update_steps = 0;

    while (model.GetCurrentTime() < end_time) {
        if (forcing_data.has_value()) {
            if (forcing_step >= forcing_data->values_.size()) {
                throw std::runtime_error(
                    "Forcing alignment error: forcing series exhausted at step " +
                    std::to_string(forcing_step));
            }

            double forcing_value = forcing_data->values_.at(forcing_step);
            model.SetValue(forcing_data->bmi_input_var_, &forcing_value);
            ++forcing_step;
        }

        model.Update();
        ++update_steps;

        const double current_time = model.GetCurrentTime();
        for (OutputVariable& variable : output_vars) {
            write_output_step(model, variable.name_, variable.n_items_,
                              current_time, variable.stream_);
        }
    }

    if (forcing_data.has_value() && forcing_step != forcing_data->values_.size()) {
        throw std::runtime_error(
            "Forcing alignment error: forcing series contains " +
            std::to_string(forcing_data->values_.size()) +
            " rows but model consumed " + std::to_string(forcing_step));
    }

    return update_steps;
}

std::size_t run_model(
    ::bmi::Bmi& model,
    const std::optional<std::string>& forcing_config_path,
    const std::string& task_output_dir) {
    // Bridge public template code to internal forcing parse/load logic
    std::optional<ForcingData> forcing_data;
    if (forcing_config_path.has_value()) {
        const ForcingConfig forcing_config =
            read_forcing_config(forcing_config_path.value());
        forcing_data = load_forcing_data(model, forcing_config);
    }
    return run_model_updates(model, forcing_data, task_output_dir);
}

} // namespace miaow::bmi
