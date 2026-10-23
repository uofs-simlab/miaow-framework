/**
 * A minimal simulation unit and runtime workflow.
 */

#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs
#include "../config.hpp"

/**
 * @brief A simple simulation unit that returns a string task argument.
 */
class HelloSimulationUnit : public miaow::SimulationUnit {
  public:
    explicit HelloSimulationUnit(std::chrono::milliseconds delay) : delay_{delay} {}

    /**
     * @brief Override execute to return the first task string argument.
     *
     * In a real simulation unit, this is where you would implement the logic to initialize
     * and run your model.
     * @param task Task payload; args[0] is expected to be a string.
     * @return Structured task result with status and message.
     */
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        std::this_thread::sleep_for(delay_);
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }

  private:
    std::chrono::milliseconds delay_;
};

int main(int argc, char* argv[]) {
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    // Read optional task inputs and the common runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        const auto config = read_example_config(argc, argv, runtime_options);

        // Register the simulation unit and construct the runtime
        miaow::SimulationUnitRegistry registry;
        const miaow::SimulationUnitId hello = registry.add<HelloSimulationUnit>(
            std::chrono::milliseconds{config.value("delay_ms", 0)});
        miaow::Runtime runtime(registry, runtime_options);

        // Submit one job containing two independent messages
        std::vector<miaow::Task> tasks{
            miaow::Task{hello, caf::config_value{"hello"},
                        miaow::TaskMetadata{{"feature_id", "greeting"}}},
            miaow::Task{hello, "hello again"},
        };
        if (config.contains("tasks")) {
            tasks.clear();
            for (const auto& input : config.at("tasks")) {
                miaow::Task task{input.value("simulation_unit_id", hello),
                                 std::vector<caf::config_value>{}};
                if (input.contains("message")) {
                    task.args_.emplace_back(input.at("message").get<std::string>());
                }
                if (input.contains("feature_id")) {
                    task.metadata_["feature_id"] =
                        input.at("feature_id").get<std::string>();
                }
                tasks.push_back(std::move(task));
            }
        }
        miaow::JobHandle job = runtime.submit_job(tasks);
        const miaow::JobRecord job_record = job.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
