#pragma once

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from bmi::Bmi.
 */
template <class BmiModelType>
class BmiSimulationUnit : public SimulationUnit {
public:
    static_assert(std::is_base_of_v<bmi::Bmi, BmiModelType>,
                  "BmiModelType must derive from bmi::Bmi");
    static_assert(std::is_default_constructible_v<BmiModelType>,
                  "BmiModelType must be default constructible");

    /**
     * @brief Execute one BMI-wrapped simulation.
     * @param task Task arguments where args[0] is the BMI config path.
     * @return Structured task result for this execution.
     */
    TaskResult execute(const Task& task) override {
        TaskResult task_result;
        task_result.simulation_unit_id_ = task.simulation_unit_id_;

        if (task.args_.empty()) {
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = "BMI error: missing config path in task specification";
            return task_result;
        }
        const std::string* config_file =
            caf::get_if<std::string>(&task.args_.front());
        if (!config_file) {
            task_result.status_ = TaskStatus::failed;
            task_result.message_ =
                "BMI error: first task argument must be a config path string";
            return task_result;
        }

        BmiModelType bmi_model;

        bool model_initialized = false;
        std::size_t update_steps = 0;

        try {
            // Initialize a fresh BMI model instance for this BMI-wrapped simulation
            bmi_model.Initialize(*config_file);
            model_initialized = true;

            // Open one CSV output file per output variable, write header rows
            std::vector<BmiOutputVariable> output_vars;
            if (!task.task_output_dir_.empty()) {
                output_vars = open_output_vars(bmi_model, task.task_output_dir_);
            }

            // Get the model end time to know when to stop updating
            const double end_time = bmi_model.GetEndTime();
            
            // Main update loop for the BMI model, writing output after each update
            while (bmi_model.GetCurrentTime() < end_time) {
                // Advance the model and increment step counter
                bmi_model.Update();
                ++update_steps;

                // Get the current model time after the update
                const double current_time = bmi_model.GetCurrentTime();
                
                // Write one row of each output variable at this time step
                for (BmiOutputVariable& v : output_vars) {
                    write_output_step(bmi_model, v.name_, v.n_items_, current_time, v.stream_);
                }
            }
            // Finalize to release BMI model resources before returning
            bmi_model.Finalize();
            model_initialized = false;
        } catch (const std::exception& exception) {
            // Catch any exceptions thrown by the BMI model, and finalize if needed
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = std::string("BMI error: ") + exception.what();
            return task_result;
        } catch (...) {
            // Catch any non-standard exceptions and finalize if needed
            if (model_initialized) {
                try {
                    bmi_model.Finalize();
                } catch (...) {
                }
            }
            task_result.status_ = TaskStatus::failed;
            task_result.message_ = "BMI error: unknown exception";
            return task_result;
        }
        // If we reach this point, the BMI workflow completed successfully
        std::ostringstream message;
        message << "BMI workflow complete (updates: " << update_steps << ")";
        task_result.status_ = TaskStatus::succeeded;
        task_result.message_ = message.str();
        return task_result;
    }

private:
    // One open output file per BMI output variable, held for the update loop
    struct BmiOutputVariable {
        std::string name_;      // Variable name as reported by the BMI model
        std::size_t n_items_;   // e.g. N for a variable on N grid nodes, 1 for a scalar
        std::ofstream stream_;  // Open CSV file stream for this variable
    };

    // Open one CSV per output variable, write the header row, return the handles
    static std::vector<BmiOutputVariable> open_output_vars(
        bmi::Bmi& model, const std::string& output_dir) {
        const std::vector<std::string> var_names = model.GetOutputVarNames();
        std::vector<BmiOutputVariable> output_vars;
        output_vars.reserve(var_names.size());
        for (const std::string& name : var_names) {
            // Byte-size ratio avoids GetVarGrid, which can return -1 for scalars
            const std::size_t n_items = static_cast<std::size_t>(
                model.GetVarNbytes(name) / model.GetVarItemsize(name));
            // Filesystem path joining handles the separator correctly
            std::ofstream stream(
                std::filesystem::path(output_dir) / (name + ".csv"));
            stream << "time";
            for (std::size_t i = 0; i < n_items; ++i) {
                stream << ",val_" << i;
            }
            stream << "\n";
            // ofstream is move-only; move into the struct rather than copying
            output_vars.push_back(BmiOutputVariable{name, n_items, std::move(stream)});
        }
        return output_vars;
    }

    // Write one CSV row: time followed by all values in the typed buffer
    template <typename T>
    static void write_var_row(bmi::Bmi& model, const std::string& name,
                              std::size_t n_items, double time, std::ofstream& out) {
        std::vector<T> buf(n_items);
        model.GetValue(name, buf.data());
        // max_digits10 gives a lossless round-trip; time is always double
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << time;
        for (const T& v : buf) {
            out << ",";
            // setprecision is a no-op for int, so only apply for floating-point
            if constexpr (std::is_floating_point_v<T>) {
                out << std::setprecision(std::numeric_limits<T>::max_digits10);
            }
            out << v;
        }
        out << "\n";
    }

    // Dispatch to the typed write_var_row based on the BMI-reported variable type
    static void write_output_step(bmi::Bmi& model, const std::string& name,
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
};

} // namespace miaow
