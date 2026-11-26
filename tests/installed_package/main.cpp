#include <iostream>
#include <utility>

#include "miaow.hpp"

int main(int, char* argv[]) {
    miaow::TaskExecutorRegistry registry;
    const miaow::TaskExecutorId executor = registry.add_subprocess(argv[1]);
    miaow::Job job;
    const miaow::SimulationUnitId unit = job.add_unit("installed_worker", executor);

    // Probe the worker's BMI dispatch without loading a model
    job.add_task(unit, "get_current_time");
    miaow::Runtime runtime(registry);
    miaow::JobHandle handle = runtime.submit_job(std::move(job));
    const miaow::JobRecord record = handle.get();
    const miaow::TaskResult& result = record.task_results_.at(0).value();
    if (result.status_ == miaow::TaskStatus::failed &&
        result.message_.starts_with("BMI error: model is not initialized")) {
        return 0;
    }

    std::cerr << "Unexpected response from the installed BMI worker: " << result.message_
              << '\n';
    return 1;
}
