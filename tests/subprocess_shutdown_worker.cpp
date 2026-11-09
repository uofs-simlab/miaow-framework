// Delay a task and executor destruction so shutdown tests can observe both phases
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>

#include <unistd.h>

#include "miaow.hpp"

namespace {

class ShutdownTaskExecutor final : public miaow::TaskExecutor {
  public:
    ~ShutdownTaskExecutor() override {
        if (!output_directory_.empty()) {
            std::ofstream{output_directory_ / "cleanup_started"} << "started\n";
            std::this_thread::sleep_for(cleanup_delay_);
            std::ofstream{output_directory_ / "cleanup_finished"} << "finished\n";
        }
    }

    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        if (task.operation_ == "configure") {
            const auto& config = caf::get<caf::config_value::dictionary>(task.args_.at(0));
            output_directory_ = task.task_output_dir_;
            execution_delay_ = std::chrono::milliseconds{
                caf::get<std::int64_t>(config.container().at("execution_delay_ms"))};
            cleanup_delay_ = std::chrono::milliseconds{
                caf::get<std::int64_t>(config.container().at("cleanup_delay_ms"))};
            return miaow::TaskResult{miaow::TaskStatus::succeeded, "worker configured",
                                     caf::config_value{
                                         static_cast<std::int64_t>(::getpid())}};
        }

        std::this_thread::sleep_for(execution_delay_);
        std::ofstream{output_directory_ / "call_finished"} << "finished\n";
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "delayed task completed"};
    }

  private:
    std::filesystem::path output_directory_;
    std::chrono::milliseconds execution_delay_{0};
    std::chrono::milliseconds cleanup_delay_{0};
};

} // namespace

int main(int argc, char* argv[]) {
    ShutdownTaskExecutor executor;
    return miaow::run_subprocess_worker(argc, argv, [&executor](const miaow::Task& task) {
        return executor.execute(task);
    });
}
