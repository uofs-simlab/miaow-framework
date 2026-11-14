#include "bmi/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <caf/settings.hpp>

namespace miaow {

namespace {

// Read a complete variable into a copied list using its native BMI type
template <class Value> TaskResult get_value(bmi_api::Bmi& model, const std::string& name) {
    const int item_size = model.GetVarItemsize(name);
    const int byte_count = model.GetVarNbytes(name);
    if (item_size != sizeof(Value) || byte_count < 0 || byte_count % item_size != 0) {
        throw std::runtime_error("variable metadata does not match its native type");
    }
    std::vector<Value> values(static_cast<std::size_t>(byte_count / item_size));
    model.GetValue(name, values.data());
    caf::config_value::list output;
    output.reserve(values.size());
    for (Value value : values) {
        if constexpr (std::is_integral_v<Value>) {
            output.emplace_back(static_cast<caf::config_value::integer>(value));
        } else {
            output.emplace_back(static_cast<double>(value));
        }
    }
    return TaskResult{TaskStatus::succeeded, {}, caf::config_value{std::move(output)}};
}

// Write a complete variable after checking the input against its native BMI type
template <class Value>
TaskResult set_value(bmi_api::Bmi& model, const std::string& name,
                     const caf::config_value::list& input) {
    const int item_size = model.GetVarItemsize(name);
    const int byte_count = model.GetVarNbytes(name);
    if (item_size != sizeof(Value) || byte_count < 0 || byte_count % item_size != 0) {
        throw std::runtime_error("variable metadata does not match its native type");
    }
    std::vector<Value> values(static_cast<std::size_t>(byte_count / item_size));
    if (input.size() != values.size()) {
        throw std::runtime_error("value count does not match the variable size");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if constexpr (std::is_integral_v<Value>) {
            const caf::config_value::integer value =
                caf::get<caf::config_value::integer>(input[index]);
            if (value < std::numeric_limits<Value>::lowest() ||
                value > std::numeric_limits<Value>::max()) {
                throw std::runtime_error("integer value is outside the native type range");
            }
            values[index] = static_cast<Value>(value);
        } else {
            const double value = caf::get<double>(input[index]);
            if constexpr (std::same_as<Value, float>) {
                if (std::isfinite(value) && (value < std::numeric_limits<float>::lowest() ||
                                             value > std::numeric_limits<float>::max())) {
                    throw std::runtime_error("real value is outside the native type range");
                }
            }
            values[index] = static_cast<Value>(value);
        }
    }
    model.SetValue(name, values.data());
    return TaskResult{TaskStatus::succeeded, {}};
}

// Seconds per unit reported by the model clock
double seconds_per_time_unit(std::string_view raw_units) {
    std::string units(raw_units);
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    units.erase(units.begin(), std::find_if(units.begin(), units.end(), not_space));
    units.erase(std::find_if(units.rbegin(), units.rend(), not_space).base(), units.end());
    std::transform(units.begin(), units.end(), units.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (units == "s" || units == "sec" || units == "second" || units == "seconds") {
        return 1.0;
    }
    if (units == "min" || units == "minute" || units == "minutes") {
        return 60.0;
    }
    if (units == "h" || units == "hr" || units == "hour" || units == "hours") {
        return 3600.0;
    }
    if (units == "d" || units == "day" || units == "days") {
        return 86400.0;
    }
    throw std::runtime_error("BMI error: unsupported time units '" + units + "'");
}

// One task owns the complete lifecycle; forcing and scientific output belong to the model
TaskResult run_model(bmi_api::Bmi& model, const std::string& init_config,
                     double tolerance_seconds) {
    bool needs_finalization = false;
    std::size_t completed_updates = 0;
    try {
        if (!std::isfinite(tolerance_seconds) || tolerance_seconds < 0.0) {
            throw std::runtime_error(
                "BMI error: time_step_tolerance_seconds must be finite and non-negative");
        }
        model.Initialize(init_config);
        needs_finalization = true;

        // Compare times in the model's units while keeping the public tolerance in seconds
        const double tolerance =
            tolerance_seconds / seconds_per_time_unit(model.GetTimeUnits());
        const double time_step = model.GetTimeStep();
        if (!std::isfinite(time_step) || time_step <= 0.0) {
            throw std::runtime_error("BMI error: time step must be finite and positive");
        }
        if (tolerance >= time_step / 2.0) {
            throw std::runtime_error("BMI error: time_step_tolerance_seconds must be less "
                                     "than half the time step");
        }

        // Check current and end times for validity and consistency
        const double end_time = model.GetEndTime();
        if (!std::isfinite(end_time)) {
            throw std::runtime_error("BMI error: end time must be finite");
        }
        double current_time = model.GetCurrentTime();
        if (!std::isfinite(current_time)) {
            throw std::runtime_error("BMI error: current time must be finite");
        }
        double remaining_time = end_time - current_time;
        if (remaining_time < -tolerance) {
            throw std::runtime_error("BMI error: current time is later than the end time");
        }

        // Main update loop: continue until the remaining time is within the tolerance
        while (remaining_time > tolerance) {
            model.Update();
            ++completed_updates;
            current_time = model.GetCurrentTime();
            if (!std::isfinite(current_time)) {
                throw std::runtime_error("BMI error: current time must be finite");
            }
            remaining_time = end_time - current_time;
        }
        if (remaining_time < -tolerance) {
            throw std::runtime_error("BMI error: update passed the end time");
        }

        // Attempt finalization once, including when the model reports a failure
        needs_finalization = false;
        model.Finalize();
    } catch (...) {
        TaskResult result = make_failed_task_result(std::current_exception(), "BMI error");
        result.message_ += " (updates: " + std::to_string(completed_updates) + ")";
        if (needs_finalization) {
            try {
                model.Finalize();
            } catch (...) {
                result.message_ += " | " + make_failed_task_result(std::current_exception(),
                                                                   "BMI finalize error")
                                               .message_;
            }
        }
        return result;
    }

    return TaskResult{TaskStatus::succeeded, "BMI workflow complete (updates: " +
                                                 std::to_string(completed_updates) + ")"};
}

} // namespace

// Resolve the worker in a source-tree build or an installed package
std::string bmi_subprocess_executable_path() {
    const std::filesystem::path build_executable{MIAOW_SUBPROCESS_PATH};
    if (std::filesystem::exists(build_executable)) {
        return build_executable.string();
    }
    const std::filesystem::path installed_executable{MIAOW_INSTALLED_SUBPROCESS_PATH};
    if (std::filesystem::exists(installed_executable)) {
        return installed_executable.string();
    }
    throw std::runtime_error("bundled BMI subprocess executable was not found");
}

BmiTaskExecutor::BmiTaskExecutor(ModelFactory model_factory)
    : model_factory_{std::move(model_factory)} {}

BmiTaskExecutor::~BmiTaskExecutor() {
    if (model_) {
        try {
            model_->Finalize();
        } catch (...) {
            // Explicit finalize tasks report errors; destruction only releases state
        }
    }
}

caf::result<TaskResult> BmiTaskExecutor::execute(const Task& task) {
    try {
        // Validate model state before dispatching the selected operation
        const bool creates_model =
            task.operation_ == "run" || task.operation_ == "initialize";
        const bool uses_retained_model =
            task.operation_ == "update" || task.operation_ == "get_current_time" ||
            task.operation_ == "get_value" || task.operation_ == "set_value" ||
            task.operation_ == "finalize";

        // if the operation creates a model, ensure no model is already retained
        if (creates_model && model_) {
            return make_failed_task_result("model is already initialized", "BMI error");
        }

        // If the operation uses a retained model, ensure a model is already retained
        if (uses_retained_model && !model_) {
            return make_failed_task_result("model is not initialized", "BMI error");
        }

        // If the model failed, it can be used only for finalization
        if (uses_retained_model && task.operation_ != "finalize" && model_failed_) {
            return make_failed_task_result(
                "model must be finalized after a failed BMI call", "BMI error");
        }

        // Full model run operation
        if (task.operation_ == "run") {
            std::unique_ptr<bmi_api::Bmi> model = model_factory_(task);
            const auto& values = caf::get<caf::config_value::dictionary>(task.args_.at(0));
            const caf::config_value* init_config = caf::get_if(&values, "init_config");
            const caf::config_value* tolerance =
                caf::get_if(&values, "time_step_tolerance_seconds");
            return run_model(*model,
                             init_config ? caf::get<std::string>(*init_config)
                                         : std::string{},
                             tolerance ? caf::get<double>(*tolerance)
                                       : default_bmi_time_step_tolerance_seconds);
        }

        // Model initialization operation
        if (task.operation_ == "initialize") {
            std::string init_config;
            if (const std::string* path = task.string_arg(0)) {
                init_config = *path;
            } else {
                const auto& values =
                    caf::get<caf::config_value::dictionary>(task.args_.at(0));
                if (const caf::config_value* path = caf::get_if(&values, "init_config")) {
                    init_config = caf::get<std::string>(*path);
                }
            }
            std::unique_ptr<bmi_api::Bmi> model = model_factory_(task);
            // Convert and destroy plugin exceptions before releasing the model
            try {
                model->Initialize(init_config);
            } catch (...) {
                return make_failed_task_result(std::current_exception(), "BMI error");
            }
            model_ = std::move(model);
            return TaskResult{TaskStatus::succeeded, {}};
        }

        // Model update operation
        if (task.operation_ == "update") {
            model_->Update();
            return TaskResult{TaskStatus::succeeded, {}};
        }

        // Get current model time operation
        if (task.operation_ == "get_current_time") {
            return TaskResult{TaskStatus::succeeded,
                              {},
                              caf::config_value{model_->GetCurrentTime()}};
        }

        // Get model variable value operation
        if (task.operation_ == "get_value") {
            const std::string& name = caf::get<std::string>(task.args_.at(0));
            const std::string type = model_->GetVarType(name);
            if (type == "double") {
                return get_value<double>(*model_, name);
            }
            if (type == "float") {
                return get_value<float>(*model_, name);
            }
            if (type == "int") {
                return get_value<int>(*model_, name);
            }
            throw std::runtime_error("unsupported BMI variable type '" + type + "'");
        }

        // Set model variable value operation
        if (task.operation_ == "set_value") {
            const std::string& name = caf::get<std::string>(task.args_.at(0));
            const std::string type = model_->GetVarType(name);
            const auto& input = caf::get<caf::config_value::list>(task.args_.at(1));
            if (type == "double") {
                return set_value<double>(*model_, name, input);
            }
            if (type == "float") {
                return set_value<float>(*model_, name, input);
            }
            if (type == "int") {
                return set_value<int>(*model_, name, input);
            }
            throw std::runtime_error("unsupported BMI variable type '" + type + "'");
        }

        // Model finalization operation
        if (task.operation_ == "finalize") {
            // Release ownership before calling Finalize so failure is not retried
            std::unique_ptr<bmi_api::Bmi> model = std::move(model_);
            model_failed_ = false;
            // Convert and destroy plugin exceptions before releasing the model
            try {
                model->Finalize();
            } catch (...) {
                return make_failed_task_result(std::current_exception(), "BMI error");
            }
            return TaskResult{TaskStatus::succeeded, {}};
        }

        // Unsupported operation handling
        return make_failed_task_result("unsupported operation '" + task.operation_ + "'",
                                       "BMI error");
    } catch (...) {
        // A failed model call permits cleanup but no further updates or queries
        model_failed_ = model_ != nullptr;
        return make_failed_task_result(std::current_exception(), "BMI error");
    }
}

DynamicBmiTaskExecutor::DynamicBmiTaskExecutor()
    : BmiTaskExecutor{[](const Task& task) -> std::unique_ptr<bmi_api::Bmi> {
          const auto& values = caf::get<caf::config_value::dictionary>(task.args_.at(0));
          const caf::config_value* backend_value = caf::get_if(&values, "backend");
          const std::string backend =
              backend_value ? caf::get<std::string>(*backend_value) : "cpp";
          if (backend != "cpp" && backend != "fortran") {
              throw std::runtime_error("BMI config error: unsupported dynamic backend '" +
                                       backend + "'");
          }
          return std::make_unique<DynamicBmiModel>(
              backend == "cpp" ? DynamicBmiBackend::cpp : DynamicBmiBackend::fortran,
              caf::get<std::string>(values.container().at("plugin_path")));
      }} {}

} // namespace miaow
