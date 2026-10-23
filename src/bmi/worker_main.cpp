#include "bmi/internal.hpp"

// Execute subprocess BMI tasks with miaow::bmi::execute_task from src/bmi/task.cpp.
// DynamicBmiSimulationUnit::execute calls the same function for in-process tasks.
// Both paths initialize the model, advance it to its end time, and finalize it.
int main(int argc, char* argv[]) {
    return miaow::run_subprocess_worker(argc, argv, miaow::bmi::execute_task);
}
