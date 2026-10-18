#define BOOST_TEST_MODULE miaow_core
#include <boost/test/unit_test.hpp>

#include <caf/binary_deserializer.hpp>
#include <caf/binary_serializer.hpp>
#include <caf/byte_buffer.hpp>
#include <caf/config_value_reader.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "io/internal.hpp"

namespace {

// Return the first string task argument without model-specific behaviour
class EchoSimulationUnit final : public miaow::SimulationUnit {
  public:
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        const std::string* message = task.string_arg(0);
        if (message == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing message"};
        }
        caf::config_value::dictionary output;
        output.emplace("echo", *message);
        return miaow::TaskResult{miaow::TaskStatus::succeeded, *message,
                                 caf::config_value{std::move(output)}};
    }
};

// Perform one Heron's method iteration for the controller integration tests
class HeronIterationSimulationUnit final : public miaow::SimulationUnit {
  public:
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
        if (task.args_.size() != 1) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "missing estimate"};
        }
        const caf::config_value::real* estimate =
            caf::get_if<caf::config_value::real>(&task.args_.front());
        if (estimate == nullptr) {
            return miaow::TaskResult{miaow::TaskStatus::failed, "invalid estimate"};
        }

        constexpr double radicand = 2.0;
        const double next_estimate = 0.5 * (*estimate + radicand / *estimate);
        const double residual = std::abs(next_estimate * next_estimate - radicand);
        caf::config_value::dictionary output;
        output.emplace("estimate", next_estimate);
        output.emplace("residual", residual);
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "iteration complete",
                                 caf::config_value{std::move(output)}};
    }
};

// Continue Heron's method until convergence or the iteration limit
class IterationJobController final : public miaow::JobController {
  public:
    IterationJobController(miaow::SimulationUnitId simulation_unit_id,
                           double residual_tolerance, std::size_t maximum_iterations)
        : simulation_unit_id_{simulation_unit_id}, residual_tolerance_{residual_tolerance},
          maximum_iterations_{maximum_iterations} {}

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        iterations_ += new_records.size();

        // Failed tasks stop the iteration without requiring structured output
        const miaow::TaskRecord& completed_task = new_records.back();
        if (completed_task.result_.status_ != miaow::TaskStatus::succeeded) {
            return {};
        }

        const caf::config_value::dictionary* output =
            caf::get_if<caf::config_value::dictionary>(&completed_task.result_.output_);
        if (output == nullptr) {
            throw std::runtime_error("JobController output error: expected a dictionary");
        }

        // Extract the estimate and residual from the output dictionary
        const caf::config_value::real* estimate =
            caf::get_if<caf::config_value::real>(output, "estimate");
        const caf::config_value::real* residual =
            caf::get_if<caf::config_value::real>(output, "residual");
        if (estimate == nullptr || residual == nullptr) {
            throw std::runtime_error(
                "JobController output error: estimate and residual must be reals");
        }
        if (*residual <= residual_tolerance_) {
            return {};
        }
        if (iterations_ >= maximum_iterations_) {
            throw std::runtime_error("Convergence error: maximum iteration count reached");
        }
        return {miaow::Task{simulation_unit_id_, caf::config_value{*estimate}}};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    double residual_tolerance_;
    std::size_t maximum_iterations_;
    std::size_t iterations_ = 0;
};

// Sleep for the duration supplied as the first task argument
class SleepSimulationUnit final : public miaow::SimulationUnit {
  public:
    caf::result<miaow::TaskResult> execute(const miaow::Task& task) override {
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
        std::span<const miaow::TaskRecord> /*new_records*/) override {
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
        std::span<const miaow::TaskRecord> /*new_records*/) override {
        throw std::runtime_error("deliberate controller failure");
    }
};

// Retain controller inputs and generate one additional batch on the first call
class RecordingJobController final : public miaow::JobController {
  public:
    RecordingJobController(
        miaow::SimulationUnitId simulation_unit_id,
        std::shared_ptr<std::vector<std::vector<miaow::TaskRecord>>> record_batches,
        miaow::TaskGenerationPolicy policy)
        : JobController(policy), simulation_unit_id_{simulation_unit_id},
          record_batches_{std::move(record_batches)} {}

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        // Record the completed task records for later inspection
        record_batches_->emplace_back(new_records.begin(), new_records.end());

        // If this is the first call to generate_new_tasks, produce an additional batch
        // with two tasks.
        if (record_batches_->size() == 1) {
            return {miaow::Task{simulation_unit_id_, "1"},
                    miaow::Task{simulation_unit_id_, "1"}};
        }
        return {};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    std::shared_ptr<std::vector<std::vector<miaow::TaskRecord>>> record_batches_;
};

// Summarize completed tasks, callbacks, and finalization in job output
class OutputJobController final : public miaow::JobController {
  public:
    using JobController::JobController;

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        completed_ += new_records.size();
        ++callbacks_;
        return {};
    }

    caf::config_value finalize() override {
        caf::config_value::dictionary output;
        output.emplace("completed", completed_);
        output.emplace("callbacks", callbacks_);
        output.emplace("finalizations", ++finalizations_);
        return caf::config_value{std::move(output)};
    }

  private:
    std::size_t completed_ = 0;
    std::size_t callbacks_ = 0;
    std::size_t finalizations_ = 0;
};

class ThrowingFinalizer final : public miaow::JobController {
  public:
    caf::config_value finalize() override {
        throw std::runtime_error("deliberate finalization failure");
    }
};

// Observe initialization and finalization independently of task execution
struct ControllerActivity {
    std::size_t initializations = 0;
    std::size_t completions = 0;
    std::size_t finalizations = 0;
};

class InitializingJobController final : public miaow::JobController {
  public:
    InitializingJobController(std::vector<miaow::Task> tasks,
                              std::shared_ptr<ControllerActivity> activity,
                              bool throw_on_initialization = false)
        : tasks_{std::move(tasks)}, activity_{std::move(activity)},
          throw_on_initialization_{throw_on_initialization} {}

    std::vector<miaow::Task> generate_initial_tasks() override {
        ++activity_->initializations;
        if (throw_on_initialization_) {
            throw std::runtime_error("deliberate initialization failure");
        }
        return std::move(tasks_);
    }

    std::vector<miaow::Task> generate_new_tasks(
        std::span<const miaow::TaskRecord> new_records) override {
        activity_->completions += new_records.size();
        return {};
    }

    caf::config_value finalize() override {
        ++activity_->finalizations;
        return {};
    }

  private:
    std::vector<miaow::Task> tasks_;
    std::shared_ptr<ControllerActivity> activity_;
    bool throw_on_initialization_;
};

} // namespace

// Initialize exactly once on controller-only submission, including an empty result
BOOST_AUTO_TEST_CASE(controller_initialization_owns_initial_tasks) {
    miaow::SimulationUnitRegistry registry;
    const auto echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime{registry, miaow::RuntimeOptions{2}};
    for (const bool empty : {false, true}) {
        auto activity = std::make_shared<ControllerActivity>();
        std::vector<miaow::Task> tasks;
        if (!empty) {
            tasks.emplace_back(echo, caf::config_value{"first"},
                               miaow::TaskMetadata{{"feature_id", "initial_first"}});
            tasks.emplace_back(echo, "second");
        }
        const auto job = runtime.submit_job(std::make_unique<InitializingJobController>(
            std::move(tasks), activity)).get();
        BOOST_TEST(activity->initializations == 1u);
        BOOST_TEST(activity->completions == (empty ? 0u : 2u));
        BOOST_TEST(activity->finalizations == 1u);
        BOOST_REQUIRE_EQUAL(job.tasks_.size(), empty ? 0u : 2u);
        if (!empty) {
            BOOST_TEST(job.tasks_[0].task_name_ == "initial_first");
            BOOST_TEST(job.tasks_[1].task_name_ == "task_1");
            BOOST_TEST(job.tasks_[0].result_.message_ == "first");
            BOOST_TEST(job.tasks_[1].result_.message_ == "second");
        }
    }
    // Explicitly submitted tasks bypass initialization, even for an empty vector
    auto activity = std::make_shared<ControllerActivity>();
    const auto job = runtime.submit_job(std::vector<miaow::Task>{},
        std::make_unique<InitializingJobController>(
            std::vector<miaow::Task>{miaow::Task{echo, "unused"}}, activity)).get();
    BOOST_TEST(job.tasks_.empty());
    BOOST_TEST(activity->initializations == 0u);
    BOOST_TEST(activity->completions == 0u);
    BOOST_TEST(activity->finalizations == 1u);
    BOOST_CHECK_THROW(runtime.submit_job(std::unique_ptr<miaow::JobController>{}),
                      std::invalid_argument);
}

// Initialization and task-preparation errors use the normal job-error path
BOOST_AUTO_TEST_CASE(controller_initialization_errors_reach_the_job_handle) {
    miaow::SimulationUnitRegistry registry;
    const auto echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime{registry};
    auto activity = std::make_shared<ControllerActivity>();
    auto handle = runtime.submit_job(std::make_unique<InitializingJobController>(
        std::vector<miaow::Task>{}, activity, true));
    BOOST_CHECK_EXCEPTION(handle.get(), std::runtime_error, [](const auto& error) {
        return std::string{error.what()}.find("deliberate initialization failure")
               != std::string::npos;
    });
    BOOST_TEST(activity->initializations == 1u);
    BOOST_TEST(activity->finalizations == 0u);

    activity = std::make_shared<ControllerActivity>();
    miaow::Task duplicate{echo, caf::config_value{"value"},
                          miaow::TaskMetadata{{"feature_id", "same"}}};
    auto invalid = runtime.submit_job(std::make_unique<InitializingJobController>(
        std::vector<miaow::Task>{duplicate, duplicate}, activity));
    BOOST_CHECK_THROW(invalid.get(), std::runtime_error);
    BOOST_TEST(activity->initializations == 1u);
    BOOST_TEST(activity->finalizations == 0u);
}

// Preserve recursively nested output through the TaskResult CAF inspector
BOOST_AUTO_TEST_CASE(task_result_output_round_trips_through_binary_serialization) {
    caf::config_value::list values;
    values.emplace_back(caf::config_value::integer{7});
    values.emplace_back("seven");
    caf::config_value::dictionary nested;
    nested.emplace("values", std::move(values));
    caf::config_value::dictionary output;
    output.emplace("nested", std::move(nested));

    const miaow::TaskResult original{miaow::TaskStatus::succeeded, "structured output",
                                     caf::config_value{std::move(output)}};
    caf::byte_buffer buffer;
    caf::binary_serializer serializer{buffer};
    BOOST_REQUIRE(serializer.apply(original));

    miaow::TaskResult restored;
    caf::binary_deserializer deserializer{buffer};
    BOOST_REQUIRE(deserializer.apply(restored));

    BOOST_CHECK(restored.status_ == original.status_);
    BOOST_TEST(restored.message_ == original.message_);
    BOOST_CHECK(restored.output_ == original.output_);
    BOOST_TEST(restored.artifact_paths_.empty());

    const miaow::TaskResult no_output{miaow::TaskStatus::succeeded, "no output"};
    caf::byte_buffer no_output_buffer;
    caf::binary_serializer no_output_serializer{no_output_buffer};
    BOOST_REQUIRE(no_output_serializer.apply(no_output));
    miaow::TaskResult restored_no_output;
    caf::binary_deserializer no_output_deserializer{no_output_buffer};
    BOOST_REQUIRE(no_output_deserializer.apply(restored_no_output));
    BOOST_TEST(!restored_no_output.output_);
}

// Preserve optional dispatch timing and worker assignment in actor messages and JSON
BOOST_AUTO_TEST_CASE(task_record_dispatch_metadata_round_trips_through_serialization) {
    const miaow::Task task{1, "input"};
    const std::vector<std::optional<std::chrono::duration<double>>> dispatch_offsets{
        std::nullopt, std::chrono::duration<double>::zero(),
        std::chrono::duration<double>{0.125}, std::chrono::duration<double>{1.23456789}};

    const auto directory = std::filesystem::path{MIAOW_CORE_TEST_OUTPUT_ROOT}
                           / "dispatch_metadata";
    std::filesystem::create_directories(directory);
    const auto path = directory / "task_record.json";

    for (const auto& dispatch_offset : dispatch_offsets) {
        std::optional<std::size_t> worker_id;
        if (dispatch_offset.has_value()) {
            worker_id = *dispatch_offset == std::chrono::duration<double>::zero() ? 0 : 2;
        }
        const miaow::TaskRecord original{
            task, 0, miaow::TaskResult{miaow::TaskStatus::succeeded, "finished"},
            std::chrono::duration<double>{0.25}, dispatch_offset, worker_id};

        caf::byte_buffer buffer;
        caf::binary_serializer serializer{buffer};
        BOOST_REQUIRE(serializer.apply(original));
        miaow::TaskRecord binary_record;
        caf::binary_deserializer deserializer{buffer};
        BOOST_REQUIRE(deserializer.apply(binary_record));
        BOOST_CHECK(binary_record.dispatch_offset_from_job_start_ == dispatch_offset);
        BOOST_CHECK(binary_record.worker_id_ == worker_id);
        BOOST_CHECK(binary_record.run_time_ == original.run_time_);

        miaow::io::write_json_file(path, original, false);
        std::ifstream input{path};
        const auto stored = nlohmann::json::parse(input);
        if (worker_id.has_value()) {
            BOOST_TEST(stored.at("worker_id").get<std::size_t>() == *worker_id);
        } else {
            BOOST_TEST(stored.at("worker_id").is_null());
        }
        if (dispatch_offset.has_value()) {
            const auto stored_offset = caf::get_as<std::chrono::duration<double>>(
                caf::config_value{stored.at("dispatch_offset_from_job_start")
                                      .get<std::string>()});
            BOOST_REQUIRE(stored_offset);
            // CAF prints six decimal places in the duration's selected unit
            BOOST_TEST(stored_offset->count() == dispatch_offset->count(),
                       boost::test_tools::tolerance(1e-6));
        } else {
            BOOST_TEST(stored.at("dispatch_offset_from_job_start").is_null());
        }
        BOOST_TEST(stored.at("run_time").get<std::string>() == "250ms");
    }
    std::filesystem::remove_all(directory);
}

// Legacy fields without dispatch metadata retain their original runtime
BOOST_AUTO_TEST_CASE(task_record_reads_fields_without_dispatch_metadata) {
    caf::config_value::dictionary result;
    result.emplace("status", "succeeded");
    result.emplace("message", "finished");
    result.emplace("artifact_paths", caf::config_value::list{});
    caf::config_value::dictionary fields;
    fields.emplace("task_id", 0);
    fields.emplace("simulation_unit_id", 1);
    fields.emplace("task_name", "task_0");
    fields.emplace("metadata", caf::config_value::dictionary{});
    fields.emplace("result", std::move(result));
    fields.emplace("run_time", "0.25s");
    const caf::config_value legacy_record{std::move(fields)};
    caf::config_value_reader reader{&legacy_record};

    miaow::TaskRecord record;
    record.dispatch_offset_from_job_start_ = std::chrono::duration<double>{1.0};
    record.worker_id_ = 2;
    BOOST_REQUIRE(reader.apply(record));
    BOOST_TEST(!record.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(!record.worker_id_.has_value());
    BOOST_TEST(record.run_time_.count() == 0.25);
}

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
    BOOST_TEST(!job_record.output_);
    BOOST_CHECK(job_record.tasks_.at(0).result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_CHECK(job_record.tasks_.at(1).result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.at(0).result_.message_ == "hello");
    BOOST_TEST(job_record.tasks_.at(1).result_.message_ == "world");

    caf::config_value::dictionary expected_hello_output;
    expected_hello_output.emplace("echo", "hello");
    caf::config_value::dictionary expected_world_output;
    expected_world_output.emplace("echo", "world");
    BOOST_CHECK(job_record.tasks_.at(0).result_.output_ ==
                caf::config_value{std::move(expected_hello_output)});
    BOOST_CHECK(job_record.tasks_.at(1).result_.output_ ==
                caf::config_value{std::move(expected_world_output)});

    const std::filesystem::path job_record_path =
        output_root / "run_0" / "job_0" / "job_record.json";
    BOOST_REQUIRE(std::filesystem::is_regular_file(job_record_path));

    std::ifstream input(job_record_path);
    const auto stored = nlohmann::json::parse(input);
    BOOST_TEST(stored.at("run_id").get<std::uint64_t>() == job_record.run_id_);
    BOOST_TEST(stored.at("job_id").get<miaow::JobId>() == job_record.job_id_);
    BOOST_REQUIRE_EQUAL(stored.at("tasks").size(), 2);
    BOOST_TEST(stored.at("tasks").at(0).at("result").at("output")
                   .at("echo").at("value").get<std::string>() == "hello");
    BOOST_TEST(stored.at("tasks").at(1).at("result").at("output")
                   .at("echo").at("value").get<std::string>() == "world");

    for (std::size_t index = 0; index < job_record.tasks_.size(); ++index) {
        const miaow::TaskRecord& record = job_record.tasks_.at(index);
        BOOST_REQUIRE(record.worker_id_.has_value());
        BOOST_TEST(*record.worker_id_ == index);
        const auto& stored_record = stored.at("tasks").at(index);
        BOOST_TEST(stored_record.at("worker_id").get<std::size_t>() == *record.worker_id_);
        const auto& dispatch_offset = record.dispatch_offset_from_job_start_;
        BOOST_REQUIRE(dispatch_offset.has_value());
        BOOST_TEST(dispatch_offset->count() >= 0.0);
        BOOST_TEST((*dispatch_offset + record.run_time_).count() <=
                   job_record.run_time_.count());
        const auto stored_offset = caf::get_as<std::chrono::duration<double>>(
            caf::config_value{stored_record.at("dispatch_offset_from_job_start")
                                  .get<std::string>()});
        BOOST_REQUIRE(stored_offset);
        // CAF duration formatting can round the persisted value in its selected unit
        BOOST_TEST(stored_offset->count() == dispatch_offset->count(),
                   boost::test_tools::tolerance(1e-6));
    }

    std::filesystem::remove_all(output_root);
}

// Queued tasks are dispatched only after the preceding task result has been recorded
BOOST_AUTO_TEST_CASE(dispatch_offsets_describe_serial_task_intervals) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1});
    const miaow::JobRecord job_record =
        runtime.submit_job({miaow::Task{echo, "first"}, miaow::Task{echo, "second"}}).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 2);
    const miaow::TaskRecord& first = job_record.tasks_.at(0);
    const miaow::TaskRecord& second = job_record.tasks_.at(1);
    BOOST_REQUIRE(first.worker_id_.has_value());
    BOOST_REQUIRE(second.worker_id_.has_value());
    BOOST_TEST(*first.worker_id_ == 0);
    BOOST_TEST(*second.worker_id_ == 0);
    BOOST_REQUIRE(first.dispatch_offset_from_job_start_.has_value());
    BOOST_REQUIRE(second.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(second.dispatch_offset_from_job_start_->count() >=
               (*first.dispatch_offset_from_job_start_ + first.run_time_).count());
}

// Use each returned estimate as the next task input until Heron's method converges
BOOST_AUTO_TEST_CASE(controller_generates_heron_iterations_until_convergence) {
    constexpr double residual_tolerance = 1e-5;
    constexpr std::size_t maximum_iterations = 20;

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId heron_iteration =
        registry.add<HeronIterationSimulationUnit>();
    miaow::Runtime runtime(registry);
    const miaow::JobRecord job_record =
        runtime
            .submit_job(std::vector<miaow::Task>{miaow::Task{heron_iteration,
                                                             caf::config_value{1.0}}},
                        std::make_unique<IterationJobController>(heron_iteration,
                                                                 residual_tolerance,
                                                                 maximum_iterations))
            .get();

    BOOST_REQUIRE(!job_record.tasks_.empty());
    BOOST_TEST(job_record.tasks_.size() <= maximum_iterations);
    const caf::config_value::dictionary* final_output =
        caf::get_if<caf::config_value::dictionary>(
            &job_record.tasks_.back().result_.output_);
    BOOST_REQUIRE(final_output != nullptr);
    const caf::config_value::real* final_residual =
        caf::get_if<caf::config_value::real>(final_output, "residual");
    BOOST_REQUIRE(final_residual != nullptr);
    BOOST_TEST(*final_residual <= residual_tolerance);
}

// Compare controller call timing with the same workload under both policies
BOOST_AUTO_TEST_CASE(controller_task_generation_policies_control_callback_timing) {
    BOOST_CHECK(miaow::JobController{}.task_generation_policy() ==
                miaow::TaskGenerationPolicy::asynchronous);

    for (const miaow::TaskGenerationPolicy policy :
         {miaow::TaskGenerationPolicy::asynchronous,
          miaow::TaskGenerationPolicy::batch_synchronous}) {
        const bool batch_synchronous =
            policy == miaow::TaskGenerationPolicy::batch_synchronous;
        const std::vector<std::size_t> expected_batch_sizes =
            batch_synchronous ? std::vector<std::size_t>{3, 2}
                              : std::vector<std::size_t>{1, 1, 1, 1, 1};

        for (const std::size_t worker_count : {1, 2}) {
            BOOST_TEST_CONTEXT("batch_synchronous = " << batch_synchronous
                                                      << ", worker_count = "
                                                      << worker_count) {
                miaow::SimulationUnitRegistry registry;
                const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
                miaow::Runtime runtime(registry, miaow::RuntimeOptions{worker_count});
                const auto record_batches =
                    std::make_shared<std::vector<std::vector<miaow::TaskRecord>>>();

                // Unequal durations exercise results arriving out of order with two
                // workers; the missing duration produces a failed result in the initial
                // tasks
                const miaow::JobRecord job_record =
                    runtime
                        .submit_job({miaow::Task{sleeper, "50"}, miaow::Task{sleeper, "1"},
                                     miaow::Task{sleeper,
                                                 std::vector<caf::config_value>{}}},
                                    std::make_unique<RecordingJobController>(
                                        sleeper, record_batches, policy))
                        .get();

                BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 5);
                BOOST_REQUIRE_EQUAL(record_batches->size(), expected_batch_sizes.size());
                std::vector<bool> seen(job_record.tasks_.size(), false);
                std::size_t delivered = 0;
                double previous_completion = 0.0;
                for (std::size_t call = 0; call < record_batches->size(); ++call) {
                    const auto& records = record_batches->at(call);
                    BOOST_REQUIRE_EQUAL(records.size(), expected_batch_sizes.at(call));
                    for (const miaow::TaskRecord& record : records) {
                        BOOST_REQUIRE(record.task_id_ < seen.size());
                        BOOST_TEST(!seen.at(record.task_id_));
                        seen.at(record.task_id_) = true;
                        if (worker_count == 1) {
                            BOOST_TEST(record.task_id_ == delivered);
                        }
                        if (batch_synchronous) {
                            BOOST_TEST((record.task_id_ < 3) == (call == 0));
                        }
                        ++delivered;

                        BOOST_CHECK(record.result_.status_ ==
                                    (record.task_id_ == 2 ? miaow::TaskStatus::failed
                                                          : miaow::TaskStatus::succeeded));
                        const miaow::TaskRecord& final_record =
                            job_record.tasks_.at(record.task_id_);
                        BOOST_REQUIRE(record.worker_id_.has_value());
                        BOOST_TEST(*record.worker_id_ < worker_count);
                        BOOST_CHECK(record.worker_id_ == final_record.worker_id_);
                        BOOST_REQUIRE(record.dispatch_offset_from_job_start_.has_value());
                        BOOST_TEST(record.dispatch_offset_from_job_start_->count() >= 0.0);
                        const double completion =
                            (*record.dispatch_offset_from_job_start_ + record.run_time_)
                                .count();
                        BOOST_TEST(completion >= previous_completion);
                        BOOST_TEST(completion <= job_record.run_time_.count());
                        previous_completion = completion;
                        BOOST_CHECK(record.dispatch_offset_from_job_start_ ==
                                    final_record.dispatch_offset_from_job_start_);
                    }
                }
            }
        }
    }
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
    const miaow::TaskRecord& timed_out_task = job_record.tasks_.at(1);
    BOOST_REQUIRE(timed_out_task.worker_id_.has_value());
    BOOST_TEST(*timed_out_task.worker_id_ == 0);
    BOOST_REQUIRE(timed_out_task.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(timed_out_task.run_time_.count() > 0.0);
    BOOST_TEST((*timed_out_task.dispatch_offset_from_job_start_ + timed_out_task.run_time_)
                   .count() <= job_record.run_time_.count());
}

// A job timeout retains dispatched intervals without assigning a start to queued tasks
BOOST_AUTO_TEST_CASE(job_timeout_distinguishes_dispatched_and_queued_task_timing) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{2, std::chrono::seconds(1),
                                                           std::chrono::milliseconds(20)});
    const miaow::JobRecord job_record =
        runtime
            .submit_job({miaow::Task{sleeper, "250"}, miaow::Task{sleeper, "250"},
                         miaow::Task{sleeper, "1"}})
            .get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 3);
    for (std::size_t index = 0; index < 2; ++index) {
        const miaow::TaskRecord& dispatched = job_record.tasks_.at(index);
        BOOST_CHECK(dispatched.result_.status_ == miaow::TaskStatus::failed);
        BOOST_REQUIRE(dispatched.worker_id_.has_value());
        BOOST_TEST(*dispatched.worker_id_ == index);
        BOOST_REQUIRE(dispatched.dispatch_offset_from_job_start_.has_value());
        BOOST_TEST(dispatched.dispatch_offset_from_job_start_->count() >= 0.0);
        BOOST_TEST(dispatched.run_time_.count() > 0.0);
        BOOST_TEST((*dispatched.dispatch_offset_from_job_start_ + dispatched.run_time_)
                       .count() <= job_record.run_time_.count());
    }
    const miaow::TaskRecord& queued = job_record.tasks_.at(2);
    BOOST_CHECK(queued.result_.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(!queued.worker_id_.has_value());
    BOOST_TEST(!queued.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(queued.run_time_.count() == 0.0);
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
    const miaow::TaskRecord& timed_out_task = job_record.tasks_.front();
    BOOST_REQUIRE(timed_out_task.worker_id_.has_value());
    BOOST_TEST(*timed_out_task.worker_id_ == 0);
    BOOST_REQUIRE(timed_out_task.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(timed_out_task.run_time_.count() > 0.0);
    BOOST_TEST((*timed_out_task.dispatch_offset_from_job_start_ + timed_out_task.run_time_)
                   .count() <= job_record.run_time_.count());
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
    BOOST_TEST(!job_record.tasks_.front().result_.output_);
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

// Finalize both controller policies and empty jobs, preserving output in CAF and JSON.
BOOST_AUTO_TEST_CASE(controller_output_is_returned_and_persisted) {
    const auto output_root =
        std::filesystem::path{MIAOW_CORE_TEST_OUTPUT_ROOT} / "job_output";
    std::filesystem::remove_all(output_root);
    miaow::SimulationUnitRegistry registry;
    const auto echo = registry.add<EchoSimulationUnit>();
    miaow::RuntimeOptions options{2};
    options.output_root_ = output_root.string();
    miaow::Runtime runtime{registry, options};

    for (const auto policy : {miaow::TaskGenerationPolicy::asynchronous,
                              miaow::TaskGenerationPolicy::batch_synchronous}) {
        for (const auto& tasks : {std::vector<miaow::Task>{miaow::Task{echo, "first"},
                                                         miaow::Task{echo, "second"}},
                                  std::vector<miaow::Task>{}}) {
            const auto job =
                runtime.submit_job(tasks, std::make_unique<OutputJobController>(policy))
                    .get();
            const auto& output = caf::get<caf::config_value::dictionary>(job.output_);
            BOOST_TEST(caf::get<caf::config_value::integer>(output, "completed")
                       == tasks.size());
            BOOST_TEST(caf::get<caf::config_value::integer>(output, "finalizations") == 1);
            const auto callbacks = policy == miaow::TaskGenerationPolicy::asynchronous
                                       ? tasks.size() : (tasks.empty() ? 0u : 1u);
            BOOST_TEST(caf::get<caf::config_value::integer>(output, "callbacks")
                       == callbacks);

            caf::byte_buffer buffer;
            caf::binary_serializer serializer{buffer};
            BOOST_REQUIRE(serializer.apply(job));
            miaow::JobRecord restored;
            caf::binary_deserializer deserializer{buffer};
            BOOST_REQUIRE_MESSAGE(deserializer.apply(restored),
                                  caf::to_string(deserializer.get_error()));
            BOOST_CHECK(restored.output_ == job.output_);

            const auto path = output_root / ("run_" + std::to_string(job.run_id_))
                              / ("job_" + std::to_string(job.job_id_)) / "job_record.json";
            std::ifstream input{path};
            const auto json = nlohmann::json::parse(input);
            BOOST_TEST(json.at("tasks").size() == tasks.size());
            const auto& stored = json.at("output");
            for (const auto key : {"completed", "callbacks", "finalizations"}) {
                BOOST_TEST(stored.at(key).at("value").get<caf::config_value::integer>()
                           == caf::get<caf::config_value::integer>(output, key));
            }
        }
    }
    std::filesystem::remove_all(output_root);
}

// Legacy fields without controller output retain the null default.
BOOST_AUTO_TEST_CASE(job_record_reads_fields_without_controller_output) {
    caf::config_value::dictionary fields;
    fields.emplace("run_id", 0);
    fields.emplace("job_id", 0);
    fields.emplace("tasks", caf::config_value::list{});
    fields.emplace("run_time", "0s");
    const caf::config_value legacy_record{std::move(fields)};
    caf::config_value_reader reader{&legacy_record};
    miaow::JobRecord job;
    job.output_ = caf::config_value{"previous output"};
    BOOST_REQUIRE(reader.apply(job));
    BOOST_TEST(!job.output_);
}

// Timeout records retain the controller's partial state even without a task result.
BOOST_AUTO_TEST_CASE(job_timeout_finalizes_controller_output) {
    miaow::SimulationUnitRegistry registry;
    const auto sleeper = registry.add<SleepSimulationUnit>();
    miaow::Runtime runtime{registry, miaow::RuntimeOptions{1, std::chrono::seconds(1),
                                                          std::chrono::milliseconds(20)}};
    const auto job =
        runtime.submit_job({miaow::Task{sleeper, "250"}},
                           std::make_unique<OutputJobController>()).get();
    BOOST_REQUIRE_EQUAL(job.tasks_.size(), 1);
    BOOST_CHECK(job.tasks_.front().result_.status_ == miaow::TaskStatus::failed);
    const auto& output = caf::get<caf::config_value::dictionary>(job.output_);
    BOOST_TEST(caf::get<caf::config_value::integer>(output, "completed") == 0);
    BOOST_TEST(caf::get<caf::config_value::integer>(output, "finalizations") == 1);
}

// Finalization errors follow the same JobHandle contract as task-generation errors.
BOOST_AUTO_TEST_CASE(controller_finalization_exception_reaches_the_job_handle) {
    miaow::SimulationUnitRegistry registry;
    const auto echo = registry.add<EchoSimulationUnit>();
    miaow::Runtime runtime{registry};
    for (const auto& tasks : {std::vector<miaow::Task>{miaow::Task{echo, "hello"}},
                              std::vector<miaow::Task>{}}) {
        auto job = runtime.submit_job(tasks, std::make_unique<ThrowingFinalizer>());
        BOOST_CHECK_EXCEPTION(job.get(), std::runtime_error,
                              [](const std::runtime_error& exception) {
                                  return std::string(exception.what()).find(
                                             "deliberate finalization failure")
                                         != std::string::npos;
                              });
    }
}
