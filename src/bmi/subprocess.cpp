#include "miaow_bmi.hpp"
#include "core/internal.hpp"

#include <exception>
#include <stdexcept>

#include <caf/all.hpp>
#include <caf/config_value.hpp>

namespace miaow {

namespace {

// Validate constraints that actor deserialization cannot express
void validate_subprocess_task(const Task& task) {
    if (task.args_.size() != 1) {
        throw std::runtime_error(
            "subprocess task must contain exactly one top-level BMI pipeline "
            "config argument");
    }
    if (caf::get_if<caf::config_value::dictionary>(&task.args_.front()) == nullptr) {
        throw std::runtime_error("subprocess task argument must be a BmiPipelineConfig");
    }
    if (!task.task_output_dir_.empty() && task.task_output_name_.empty()) {
        throw std::runtime_error(
            "subprocess task key 'task_output_name' must be a non-empty string");
    }
}

// Run exactly one BMI pipeline task and then stop the worker actor
caf::behavior bmi_worker_actor(caf::event_based_actor* self) {
    return {[self](caf::get_atom, const Task& task) -> TaskResult {
        TaskResult task_result;
        try {
            // Config-constructible pipelines build all models in the subprocess
            validate_subprocess_task(task);
            BmiPipeline pipeline(task);
            task_result = pipeline.execute_bmi_lifecycle(task);
        } catch (...) {
            // Exceptions cannot propagate through the subprocess boundary
            task_result = make_failed_task_result(std::current_exception(), "BMI error");
        }
        self->quit();
        return task_result;
    }};
}

} // namespace

} // namespace miaow

// Entry point for subprocess execution launched via Boost.Process
int main(int argc, char* argv[]) {
    return miaow::core::run_single_task_subprocess(argc, argv, miaow::bmi_worker_actor);
}
