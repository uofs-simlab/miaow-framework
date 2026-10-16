#define BOOST_TEST_MODULE miaow_core
#include <boost/test/unit_test.hpp>

#include <caf/binary_deserializer.hpp>
#include <caf/binary_serializer.hpp>
#include <caf/byte_buffer.hpp>
#include <caf/json_reader.hpp>
#include <caf/json_writer.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
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
        const std::vector<miaow::TaskRecord>& completed_task_records) override {
        const caf::config_value::dictionary* output =
            caf::get_if<caf::config_value::dictionary>(
                &completed_task_records.back().result_.output_);
        if (output == nullptr) {
            throw std::runtime_error("missing output dictionary");
        }

        const caf::config_value::dictionary::const_iterator estimate_entry =
            output->find("estimate");
        const caf::config_value::dictionary::const_iterator residual_entry =
            output->find("residual");
        if (estimate_entry == output->end() || residual_entry == output->end()) {
            throw std::runtime_error("missing estimate or residual");
        }

        const caf::config_value::real* estimate =
            caf::get_if<caf::config_value::real>(&estimate_entry->second);
        const caf::config_value::real* residual =
            caf::get_if<caf::config_value::real>(&residual_entry->second);
        if (estimate == nullptr || residual == nullptr) {
            throw std::runtime_error("invalid estimate or residual");
        }
        if (*residual <= residual_tolerance_) {
            return {};
        }
        if (completed_task_records.size() >= maximum_iterations_) {
            throw std::runtime_error("maximum iteration count reached");
        }
        return {miaow::Task{simulation_unit_id_, caf::config_value{*estimate}}};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    double residual_tolerance_;
    std::size_t maximum_iterations_;
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

// Retain controller inputs and generate one additional batch on the first call
class RecordingJobController final : public miaow::JobController {
  public:
    RecordingJobController(
        miaow::SimulationUnitId simulation_unit_id,
        std::shared_ptr<std::vector<std::vector<miaow::TaskRecord>>> record_histories,
        miaow::TaskGenerationPolicy policy)
        : JobController(policy), simulation_unit_id_{simulation_unit_id},
          record_histories_{std::move(record_histories)} {}

    std::vector<miaow::Task> generate_new_tasks(
        const std::vector<miaow::TaskRecord>& completed_task_records) override {
        // Record the completed task records for later inspection
        record_histories_->push_back(completed_task_records);

        // If this is the first call to generate_new_tasks, produce an additional batch
        // with two tasks.
        if (record_histories_->size() == 1) {
            return {miaow::Task{simulation_unit_id_, "1"},
                    miaow::Task{simulation_unit_id_, "1"}};
        }
        return {};
    }

  private:
    miaow::SimulationUnitId simulation_unit_id_;
    std::shared_ptr<std::vector<std::vector<miaow::TaskRecord>>> record_histories_;
};

} // namespace

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

// Preserve missing, zero, and positive dispatch offsets in actor messages and JSON
BOOST_AUTO_TEST_CASE(task_record_dispatch_offset_round_trips_through_serialization) {
    const miaow::Task task{1, "input"};
    const std::vector<std::optional<std::chrono::duration<double>>> dispatch_offsets{
        std::nullopt, std::chrono::duration<double>::zero(),
        std::chrono::duration<double>{0.125}, std::chrono::duration<double>{1.23456789}};

    for (const auto& dispatch_offset : dispatch_offsets) {
        const miaow::TaskRecord original{
            task, 0, miaow::TaskResult{miaow::TaskStatus::succeeded, "finished"},
            std::chrono::duration<double>{0.25}, dispatch_offset};

        caf::byte_buffer buffer;
        caf::binary_serializer serializer{buffer};
        BOOST_REQUIRE(serializer.apply(original));
        miaow::TaskRecord binary_record;
        caf::binary_deserializer deserializer{buffer};
        BOOST_REQUIRE(deserializer.apply(binary_record));
        BOOST_CHECK(binary_record.dispatch_offset_from_job_start_ == dispatch_offset);
        BOOST_CHECK(binary_record.run_time_ == original.run_time_);

        caf::json_writer writer;
        writer.skip_object_type_annotation(true);
        writer.skip_empty_fields(false);
        BOOST_REQUIRE(writer.apply(original));
        caf::json_reader reader;
        BOOST_REQUIRE(reader.load(writer.str()));
        miaow::TaskRecord json_record;
        BOOST_REQUIRE(reader.apply(json_record));
        const auto& json_offset = json_record.dispatch_offset_from_job_start_;
        BOOST_REQUIRE(json_offset.has_value() == dispatch_offset.has_value());
        if (dispatch_offset.has_value()) {
            // CAF prints six decimal places in the duration's selected unit
            BOOST_TEST(json_offset->count() == dispatch_offset->count(),
                       boost::test_tools::tolerance(1e-6));
        }
        BOOST_CHECK(json_record.run_time_ == original.run_time_);
    }
}

// Saved records without dispatch timing retain their original runtime
BOOST_AUTO_TEST_CASE(task_record_reads_json_without_a_dispatch_offset) {
    caf::json_reader reader;
    BOOST_REQUIRE(reader.load(R"({
        "task_id": 0,
        "simulation_unit_id": 1,
        "task_name": "task_0",
        "metadata": {},
        "result": {"status": "succeeded", "message": "finished", "artifact_paths": []},
        "run_time": "0.25s"
    })"));

    miaow::TaskRecord record;
    record.dispatch_offset_from_job_start_ = std::chrono::duration<double>{1.0};
    BOOST_REQUIRE(reader.apply(record));
    BOOST_TEST(!record.dispatch_offset_from_job_start_.has_value());
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
    BOOST_TEST(std::filesystem::file_size(job_record_path) > 0);

    caf::json_reader reader;
    BOOST_REQUIRE(reader.load_file(job_record_path.string()));
    miaow::JobRecord stored_job_record;
    BOOST_REQUIRE(reader.apply(stored_job_record));
    BOOST_REQUIRE_EQUAL(stored_job_record.tasks_.size(), 2);
    BOOST_CHECK(stored_job_record.tasks_.at(0).result_.output_ ==
                job_record.tasks_.at(0).result_.output_);
    BOOST_CHECK(stored_job_record.tasks_.at(1).result_.output_ ==
                job_record.tasks_.at(1).result_.output_);

    for (std::size_t index = 0; index < job_record.tasks_.size(); ++index) {
        const miaow::TaskRecord& record = job_record.tasks_.at(index);
        const auto& dispatch_offset = record.dispatch_offset_from_job_start_;
        BOOST_REQUIRE(dispatch_offset.has_value());
        BOOST_TEST(dispatch_offset->count() >= 0.0);
        BOOST_TEST((*dispatch_offset + record.run_time_).count() <=
                   job_record.run_time_.count());
        const auto& stored_offset =
            stored_job_record.tasks_.at(index).dispatch_offset_from_job_start_;
        BOOST_REQUIRE(stored_offset.has_value());
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
    const caf::config_value::dictionary::const_iterator residual_entry =
        final_output->find("residual");
    BOOST_REQUIRE(residual_entry != final_output->end());
    const caf::config_value::real* final_residual =
        caf::get_if<caf::config_value::real>(&residual_entry->second);
    BOOST_REQUIRE(final_residual != nullptr);
    BOOST_TEST(*final_residual <= residual_tolerance);
}

// Report non-convergence when Heron's method reaches its iteration limit
BOOST_AUTO_TEST_CASE(controller_reports_the_heron_iteration_limit) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId heron_iteration =
        registry.add<HeronIterationSimulationUnit>();
    miaow::Runtime runtime(registry);
    miaow::JobHandle job = runtime.submit_job(
        std::vector<miaow::Task>{miaow::Task{heron_iteration, caf::config_value{1.0}}},
        std::make_unique<IterationJobController>(heron_iteration, 1e-20, 1));

    BOOST_CHECK_EXCEPTION(
        job.get(), std::runtime_error, [](const std::runtime_error& exception) {
            return std::string(exception.what()).find("maximum iteration count reached") !=
                   std::string::npos;
        });
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
        const std::vector<std::size_t> expected_history_sizes =
            batch_synchronous ? std::vector<std::size_t>{3, 5}
                              : std::vector<std::size_t>{1, 2, 3, 4, 5};

        for (const std::size_t worker_count : {1, 2}) {
            BOOST_TEST_CONTEXT("batch_synchronous = " << batch_synchronous
                                                      << ", worker_count = "
                                                      << worker_count) {
                miaow::SimulationUnitRegistry registry;
                const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
                miaow::Runtime runtime(registry, miaow::RuntimeOptions{worker_count});
                const auto record_histories =
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
                                        sleeper, record_histories, policy))
                        .get();

                BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 5);
                BOOST_REQUIRE_EQUAL(record_histories->size(),
                                    expected_history_sizes.size());
                const std::vector<miaow::TaskRecord>& full_history =
                    record_histories->back();
                BOOST_REQUIRE_EQUAL(full_history.size(), 5);
                for (const miaow::TaskRecord& record : full_history) {
                    const miaow::TaskRecord& final_record =
                        job_record.tasks_.at(record.task_id_);
                    BOOST_REQUIRE(record.dispatch_offset_from_job_start_.has_value());
                    BOOST_TEST(record.dispatch_offset_from_job_start_->count() >= 0.0);
                    BOOST_TEST(
                        (*record.dispatch_offset_from_job_start_ + record.run_time_)
                            .count() <= job_record.run_time_.count());
                    BOOST_CHECK(record.dispatch_offset_from_job_start_ ==
                                final_record.dispatch_offset_from_job_start_);
                }
                for (std::size_t call = 0; call < record_histories->size(); ++call) {
                    const std::vector<miaow::TaskRecord>& history =
                        record_histories->at(call);
                    BOOST_REQUIRE_EQUAL(history.size(), expected_history_sizes.at(call));
                    for (std::size_t index = 0; index < history.size(); ++index) {
                        const miaow::TaskRecord& record = history.at(index);
                        BOOST_CHECK(record.result_.status_ ==
                                    (record.task_id_ == 2 ? miaow::TaskStatus::failed
                                                          : miaow::TaskStatus::succeeded));
                        BOOST_TEST(full_history.at(index).task_id_ == record.task_id_);
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
    BOOST_REQUIRE(timed_out_task.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(timed_out_task.run_time_.count() > 0.0);
    BOOST_TEST((*timed_out_task.dispatch_offset_from_job_start_ + timed_out_task.run_time_)
                   .count() <= job_record.run_time_.count());
}

// A job timeout retains dispatched intervals without assigning a start to queued tasks
BOOST_AUTO_TEST_CASE(job_timeout_distinguishes_dispatched_and_queued_task_timing) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId sleeper = registry.add<SleepSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::seconds(1),
                                                           std::chrono::milliseconds(20)});
    const miaow::JobRecord job_record =
        runtime.submit_job({miaow::Task{sleeper, "250"}, miaow::Task{sleeper, "1"}}).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 2);
    const miaow::TaskRecord& dispatched = job_record.tasks_.at(0);
    const miaow::TaskRecord& queued = job_record.tasks_.at(1);
    BOOST_CHECK(dispatched.result_.status_ == miaow::TaskStatus::failed);
    BOOST_CHECK(queued.result_.status_ == miaow::TaskStatus::failed);
    BOOST_REQUIRE(dispatched.dispatch_offset_from_job_start_.has_value());
    BOOST_TEST(dispatched.dispatch_offset_from_job_start_->count() >= 0.0);
    BOOST_TEST(dispatched.run_time_.count() > 0.0);
    BOOST_TEST((*dispatched.dispatch_offset_from_job_start_ + dispatched.run_time_).count()
               <= job_record.run_time_.count());
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
