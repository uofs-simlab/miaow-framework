#include "bmi/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <caf/settings.hpp>

namespace miaow {

Task make_bmi_task(SimulationUnitId simulation_unit_id, const StaticBmiConfig& config) {
    caf::config_value::dictionary values;
    values.insert("init_config", config.init_config_);
    values.insert("time_step_tolerance_seconds", config.time_step_tolerance_seconds_);
    return Task{simulation_unit_id, caf::config_value{std::move(values)}};
}

Task make_bmi_task(SimulationUnitId simulation_unit_id, const DynamicBmiConfig& config) {
    caf::config_value::dictionary values;
    values.insert("backend", config.backend_ == DynamicBmiBackend::cpp ? "cpp" : "fortran");
    values.insert("plugin_path", config.plugin_path_);
    values.insert("init_config", config.init_config_);
    values.insert("time_step_tolerance_seconds", config.time_step_tolerance_seconds_);
    return Task{simulation_unit_id, caf::config_value{std::move(values)}};
}

namespace {

// Decode the single-model configuration carried by a BMI task
StaticBmiConfig read_static_config(const Task& task) {
    const auto& values = caf::get<caf::config_value::dictionary>(task.args_.at(0));
    return StaticBmiConfig{
        .init_config_ = caf::get<std::string>(values, "init_config"),
        .time_step_tolerance_seconds_ =
            caf::get<double>(values, "time_step_tolerance_seconds"),
    };
}

DynamicBmiConfig read_dynamic_config(const Task& task) {
    const auto& values = caf::get<caf::config_value::dictionary>(task.args_.at(0));
    const auto& backend = caf::get<std::string>(values, "backend");
    if (backend != "cpp" && backend != "fortran") {
        throw std::runtime_error("BMI config error: unsupported dynamic backend '" +
                                 backend + "'");
    }
    return DynamicBmiConfig{
        .backend_ = backend == "cpp" ? DynamicBmiBackend::cpp : DynamicBmiBackend::fortran,
        .plugin_path_ = caf::get<std::string>(values, "plugin_path"),
        .init_config_ = caf::get<std::string>(values, "init_config"),
        .time_step_tolerance_seconds_ =
            caf::get<double>(values, "time_step_tolerance_seconds"),
    };
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
    bool finalization_pending = false;
    std::size_t completed_updates = 0;
    try {
        if (!std::isfinite(tolerance_seconds) || tolerance_seconds < 0.0) {
            throw std::runtime_error(
                "BMI error: time_step_tolerance_seconds must be finite and non-negative");
        }
        model.Initialize(init_config);
        finalization_pending = true;

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
        finalization_pending = false;
        model.Finalize();
    } catch (...) {
        TaskResult result = make_failed_task_result(std::current_exception(), "BMI error");
        result.message_ += " (updates: " + std::to_string(completed_updates) + ")";
        if (finalization_pending) {
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

namespace bmi {

TaskResult execute_task(const Task& task) {
    try {
        const DynamicBmiConfig config = read_dynamic_config(task);
        DynamicBmiModel model(config);
        return run_model(model, config.init_config_, config.time_step_tolerance_seconds_);
    } catch (...) {
        return make_failed_task_result(std::current_exception(), "BMI error");
    }
}

} // namespace bmi

TaskResult static_bmi_execution::execute_task(const Task& task,
                                              std::unique_ptr<bmi_api::Bmi> model) {
    const StaticBmiConfig config = read_static_config(task);
    return run_model(*model, config.init_config_, config.time_step_tolerance_seconds_);
}

caf::result<TaskResult> DynamicBmiSimulationUnit::execute(const Task& task) {
    return bmi::execute_task(task);
}

} // namespace miaow
