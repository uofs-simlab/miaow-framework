#include "miaow_bmi.hpp"

#include "bmi/internal.hpp"

#include <exception>
#include <stdexcept>

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
    if (!task.task_output_dir_.empty() && task.task_name_.empty()) {
        throw std::runtime_error(
            "subprocess task key 'task_name' must be a non-empty string");
    }
}

// Execute one BMI pipeline task received by the generic subprocess worker
TaskResult execute_bmi_subprocess_task(const Task& task) {
    try {
        // Config-constructible pipelines build all models in the subprocess
        validate_subprocess_task(task);
        bmi::BmiPipeline pipeline;
        return pipeline.execute_bmi_lifecycle(task);
    } catch (...) {
        // Exceptions cannot propagate through the subprocess boundary
        return make_failed_task_result(std::current_exception(), "BMI error");
    }
}

} // namespace

} // namespace miaow

// Entry point for subprocess execution launched through the POSIX process boundary
int main(int argc, char* argv[]) {
    return miaow::run_subprocess_worker(argc, argv, miaow::execute_bmi_subprocess_task);
}
