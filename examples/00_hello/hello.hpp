#pragma once

/**
 * A minimal task executor and runtime workflow.
 */

#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp" // Core MIAOW runtime API
#include <nlohmann/json.hpp>

namespace miaow::examples::hello {

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

inline std::vector<GreetingInput> parse_greetings(const nlohmann::json& config,
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

inline miaow::Job make_job(miaow::TaskExecutorRegistry& registry,
                           const nlohmann::json& config) {
    const std::chrono::milliseconds delay{config.value("delay_ms", 0)};

    // Register the executor so task inputs can resolve their default executor ID
    const miaow::TaskExecutorId hello = registry.add<HelloTaskExecutor>(delay);
    const std::vector<GreetingInput> inputs = parse_greetings(config, hello);

    // Each greeting gets its own unit; all JSON defaults have already been resolved
    miaow::Job job;
    for (const GreetingInput& input : inputs) {
        const miaow::SimulationUnitId unit =
            job.add_unit(input.feature_id, input.executor_id);

        // Pass a supplied message, or let the executor report that it is missing
        if (input.message) {
            job.add_task(unit, "greet", *input.message);
        } else {
            job.add_task(unit, "greet");
        }
    }

    return job;
}

} // namespace miaow::examples::hello
