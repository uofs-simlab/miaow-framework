/**
 * A minimal task executor and runtime workflow.
 */

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp"    // Core MIAOW runtime API
#include "miaow_io.hpp" // I/O helpers for example programs
#include "../config.hpp"

/**
 * @brief A simple task executor that returns a string task argument.
 */
class HelloTaskExecutor : public miaow::TaskExecutor {
  public:
    explicit HelloTaskExecutor(std::chrono::milliseconds delay) : delay_{delay} {}

    /**
     * @brief Override execute to return the first task string argument.
     *
     * In a real task executor, this is where you would implement the logic to initialize
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

// Parsed inputs keep JSON access separate from unit and task construction
struct GreetingInput {
    std::string feature_id;
    miaow::TaskExecutorId executor_id;
    std::optional<std::string> message;
};

std::vector<GreetingInput> parse_greetings(const nlohmann::json& config,
                                           miaow::TaskExecutorId default_executor) {
    // Default task inputs; the first includes a feature ID, the second uses a generated ID
    const nlohmann::json default_tasks = {
        {{"message", "hello"}, {"feature_id", "greeting"}},
        {{"message", "hello again"}},
    };

    // Use defaults only when "tasks" is absent; an explicit empty array stays empty
    const nlohmann::json tasks = config.value("tasks", default_tasks);
    std::vector<GreetingInput> inputs;
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        const nlohmann::json& task = tasks[index];
        GreetingInput input{task.value("feature_id", "message_" + std::to_string(index)),
                            task.value("task_executor_id", default_executor), std::nullopt};

        // Preserve a missing message so the executor can report it as a task failure
        if (task.contains("message")) {
            input.message = task.at("message").get<std::string>();
        }
        inputs.push_back(std::move(input));
    }
    return inputs;
}

int main(int argc, char* argv[]) {
    // Require the example directory so relative input paths resolve correctly
    if (!miaow::ensure_expected_run_dir(MIAOW_HELLO_RUN_DIR)) {
        return 1;
    }

    // Read optional task inputs and the common runtime options
    miaow::RuntimeOptions runtime_options;
    try {
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);

        const std::chrono::milliseconds delay{config.value("delay_ms", 0)};

        // Register the executor so task inputs can resolve their default executor ID
        miaow::TaskExecutorRegistry registry;
        const miaow::TaskExecutorId hello = registry.add<HelloTaskExecutor>(delay);
        const std::vector<GreetingInput> inputs = parse_greetings(config, hello);
        miaow::Runtime runtime(registry, runtime_options);

        // Each greeting gets its own unit; all JSON defaults have already been resolved
        miaow::Job job;
        for (const GreetingInput& input : inputs) {
            const miaow::SimulationUnitHandle unit =
                job.add_unit(input.feature_id, input.executor_id);

            // Pass a supplied message, or let the executor report that it is missing
            if (input.message) {
                job.add_task(unit, "greet", *input.message);
            } else {
                job.add_task(unit, "greet");
            }
        }

        // Submit the job
        miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

        // Wait for completion and retrieve the job record
        const miaow::JobRecord job_record = job_handle.get();

        // Print task results and aggregate timing information
        miaow::print_job_summary(job_record);

        // Return 0 if all tasks succeeded, or 1 if any task failed
        return miaow::all_tasks_succeeded(job_record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
