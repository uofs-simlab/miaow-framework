/**
 * A minimal simulation unit and runtime workflow.
 */

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs

/**
 * @brief A simple simulation unit that returns a string task argument.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
  public:
    /**
     * @brief Override execute to return the first task string argument.
     * In a real simulation unit, this is where you would implement the logic to initialize
     * and run your model.
     * @param task Task payload; args[0] is expected to be a string.
     * @return Structured task result with status and message.
     */
    miaow::TaskResult execute(const miaow::Task& task,
                              miaow::ExecutionContext& /*context*/) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }
};

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    // Initialize runtime options and parse runtime CLI arguments
    miaow::RuntimeOptions runtime_options;
    try {
        miaow::parse_runtime_cli_options(argc, argv, runtime_options);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    // Register the simulation unit and construct the runtime
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId hello = registry.add<HelloSimulationUnit>();
    miaow::Runtime runtime(registry, runtime_options);

    // Submit one job containing two independent messages
    const std::vector<miaow::Task> tasks{
        miaow::Task{hello, "hello"},
        miaow::Task{hello, "hello again"},
    };
    miaow::JobHandle job = runtime.submit_job(tasks);
    const miaow::JobRecord job_record = job.get();

    // Print task results and aggregate timing information
    miaow::print_job_summary(job_record);

    // Return 0 if all tasks succeeded, or 1 if any task failed
    return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
}
