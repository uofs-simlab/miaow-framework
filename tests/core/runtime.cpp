#define BOOST_TEST_MODULE miaow_core
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp"

namespace {

// Return the first string task argument without model-specific behaviour
class EchoSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message};
    }
};

// Sleep for the duration supplied as the first task argument
class SleepSimulationUnit final : public miaow::SimulationUnit {
  public:
    miaow::TaskResult execute(const miaow::Task& task) override {
        const std::string* duration = task.string_arg(0);
        if (duration == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing duration"};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(*duration)));
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "finished"};
    }
};

// Generate one task that exceeds the complete job deadline
class TimeoutJobController final : public miaow::JobController {
  public:
    explicit TimeoutJobController(miaow::SimulationUnitId simulation_unit_id)
        : simulation_unit_id_{simulation_unit_id} {}

    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& /*completed_task_records*/) override {
        if (generated_task_) {
            return {};
        }
        generated_task_ = true;
        return {miaow::Task{simulation_unit_id_, "250"}};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    bool generated_task_{false};
};

// Report a controller failure after the initial task finishes
class ThrowingJobController final : public miaow::JobController {
  public:
    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& /*completed_task_records*/) override {
        throw std::runtime_error("deliberate controller failure");
    }
};

} // namespace

// Enforce the Runtime requirement for at least one registered simulation unit
BOOST_AUTO_TEST_CASE(runtime_rejects_empty_registry) {
    const miaow::SimulationUnitRegistry registry;

    BOOST_CHECK_THROW(miaow::Runtime{registry}, std::invalid_argument);
}

// Exercise concurrent in-process tasks and runtime-managed job record output
BOOST_AUTO_TEST_CASE(runtime_executes_tasks_and_writes_a_job_record) {
    const std::filesystem::path output_root = MIAOW_CORE_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    // Run two model-independent tasks with runtime-managed output enabled
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{2, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});
    const miaow::JobRecord job_record =
        runtime
            .submit_job(std::vector<miaow::Task>{miaow::Task{echo, "hello"},
                                                 miaow::Task{echo, "world"}})
            .get();

    // Check the public result and the one durable runtime output contract
    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 2);
    BOOST_CHECK(job_record.tasks_.at(0).result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_CHECK(job_record.tasks_.at(1).result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.at(0).result_.message_ == "hello");
    BOOST_TEST(job_record.tasks_.at(1).result_.message_ == "world");

    const std::filesystem::path job_record_path =
        output_root / "run_0" / "job_0" / "job_record.json";
    BOOST_REQUIRE(std::filesystem::is_regular_file(job_record_path));
    BOOST_TEST(std::filesystem::file_size(job_record_path) > 0);

    std::filesystem::remove_all(output_root);
}

// Reject feature identifiers that would assign the same artifact name to two tasks
BOOST_AUTO_TEST_CASE(duplicate_feature_ids_are_rejected) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry);

    std::vector<miaow::Task> tasks{
        miaow::Task{echo, "first"},
        miaow::Task{echo, "second"},
    };
    tasks.at(0).metadata_["feature_id"] = "catchment-1";
    tasks.at(1).metadata_["feature_id"] = "catchment-1";

    BOOST_CHECK_EXCEPTION(runtime.submit_job(std::move(tasks)), std::runtime_error,
                          [](const std::runtime_error& exception) {
                              return std::string(exception.what()) ==
                                     "Task name error: duplicate task name 'catchment-1'";
                          });
}

// Reject both path separator forms before feature identifiers become artifact names
BOOST_AUTO_TEST_CASE(feature_id_path_separators_are_rejected) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry);

    for (const std::string& feature_id : {"catchment/1", "catchment\\1"}) {
        miaow::Task task{echo, "hello"};
        task.metadata_["feature_id"] = feature_id;

        BOOST_CHECK_EXCEPTION(
            runtime.submit_job(task), std::runtime_error,
            [](const std::runtime_error& exception) {
                return std::string(exception.what()) ==
                       "Task name error: feature_id must not contain path separators";
            });
    }
}

// Include controller-generated work when the complete job reaches its deadline
BOOST_AUTO_TEST_CASE(job_timeout_includes_a_generated_task) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::seconds(1),
                                                           std::chrono::milliseconds(100)});

    const miaow::JobRecord job_record =
        runtime
            .submit_job(std::vector<miaow::Task>{miaow::Task{sleeper, "1"}},
                        std::make_unique<TimeoutJobController>(sleeper))
            .get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 2);
    BOOST_CHECK(job_record.tasks_.at(0).result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_CHECK(job_record.tasks_.at(1).result_.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(job_record.tasks_.at(1).result_.message_ ==
               "Job timeout: configured job timeout expired");
}

// Apply the per-task request deadline independently of the complete job deadline
BOOST_AUTO_TEST_CASE(task_timeout_marks_task_failed) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::milliseconds(20),
                                                           std::chrono::seconds(1)});

    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::Task{sleeper, "100"}).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(job_record.tasks_.front().result_.message_.starts_with("CAF error:"));
    BOOST_TEST(job_record.tasks_.front().result_.message_.find("request_timeout") !=
               std::string::npos);
}

// Convert an unregistered SimulationUnitId into a failed task result
BOOST_AUTO_TEST_CASE(unknown_simulation_unit_id_marks_task_failed) {
    miaow::SimulationUnitRegistry registry;
    registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry);

    constexpr miaow::SimulationUnitId unknown_simulation_unit_id = 999;
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::Task{unknown_simulation_unit_id, "hello"}).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(job_record.tasks_.front().result_.message_.find(
                   "unknown SimulationUnitId: 999") != std::string::npos);
}

// Propagate JobController exceptions through JobHandle::get
BOOST_AUTO_TEST_CASE(controller_exception_reaches_the_job_handle) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry);

    miaow::JobHandle job =
        runtime.submit_job(std::vector<miaow::Task>{miaow::Task{echo, "hello"}},
                           std::make_unique<ThrowingJobController>());

    BOOST_CHECK_EXCEPTION(
        job.get(), std::runtime_error, [](const std::runtime_error& exception) {
            return std::string(exception.what()).find("deliberate controller failure") !=
                   std::string::npos;
        });
}
