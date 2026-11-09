#include "miaow_bmi.hpp"

// Retain one dynamic BMI executor across the subprocess's tasks
int main(int argc, char* argv[]) {
    miaow::DynamicBmiTaskExecutor executor;
    return miaow::run_subprocess_worker(argc, argv, [&executor](const miaow::Task& task) {
        return executor.execute(task);
    });
}
