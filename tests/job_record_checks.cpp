// Execute shared workloads and check numerical results before inspecting the final report
#define BOOST_TEST_MODULE miaow_job_record_checks
#include <boost/test/unit_test.hpp>
#include <boost/io/ios_state.hpp>
#include <nlohmann/json.hpp>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "core/build_info.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace miaow::tests {
miaow::JobRecord run_workload(const nlohmann::json& config, miaow::RuntimeOptions options);
}

namespace {

struct JobRecordChecker {
    JobRecordChecker() {
        const auto& arguments = boost::unit_test::framework::master_test_suite();
        // Arguments after -- are the output root and optional case file
        BOOST_REQUIRE(arguments.argc >= 3);
        const std::filesystem::path output_root{arguments.argv[1]};
        record_path = output_root / "run_0" / "job_0" / "job_record.json";
        if (std::string_view{arguments.argv[2]} != "") {
            std::ifstream input{arguments.argv[2]};
            test_case = nlohmann::json::parse(input);
        }

        // Reuse CLI validation and execute each workload once in this test process
        int exit_code = 0;
        try {
            miaow::RuntimeOptions options;
            std::vector<std::string> runtime_options{
                "--max_workers",
                test_case.value("max_workers", nlohmann::json(2)).dump(),
                "--task_request_timeout",
                test_case.value("task_request_timeout", nlohmann::json(10)).dump(),
                "--job_request_timeout",
                test_case.value("job_request_timeout", nlohmann::json(20)).dump(),
                "--output_root",
                output_root.string()};
            std::vector<char*> runtime_arguments{arguments.argv[0]};
            for (std::string& option : runtime_options) {
                runtime_arguments.push_back(option.data());
            }
            miaow::parse_runtime_cli_options(static_cast<int>(runtime_arguments.size()),
                                             runtime_arguments.data(), options);
            record = miaow::tests::run_workload(test_case.value("input",
                                                                nlohmann::json::object()),
                                                options);
            exit_code = miaow::all_tasks_succeeded(record) ? 0 : 1;
        } catch (const std::exception& exception) {
            exit_code = 1;
            execution_error = exception.what();
        }
        BOOST_REQUIRE_EQUAL(exit_code, test_case.value("exit_code", 0));
        if (test_case.contains("error")) {
            BOOST_TEST(execution_error.find(test_case.at("error").get<std::string>()) !=
                       std::string::npos);
        }
        if (!execution_error.empty()) {
            return;
        }

        // Check the report schema and its correspondence with the returned record
        std::ifstream input{record_path};
        job_record = nlohmann::json::parse(input);
        BOOST_TEST(job_record.at("job_id").is_number_unsigned());
        BOOST_TEST(job_record.at("miaow_version").get<std::string>() ==
                   miaow::core::build_version);
        BOOST_TEST(job_record.at("summary").at("task_count") ==
                   record.task_results_.size());
        const auto& profile = job_record.at("profile");
        const std::vector<std::string> columns{"task_id",
                                               "unit_id",
                                               "operation_id",
                                               "status",
                                               "dispatch_offset_ns",
                                               "completion_offset_ns"};
        BOOST_TEST(profile.at("columns") == columns);
        BOOST_REQUIRE(profile.at("tasks").is_array());
        BOOST_REQUIRE_EQUAL(profile.at("tasks").size(), record.task_results_.size());
        nlohmann::json failures = nlohmann::json::array();
        nlohmann::json artifacts = nlohmann::json::array();
        for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
            const auto& row = profile.at("tasks").at(id);
            BOOST_TEST(row.at(0).is_number_unsigned());
            BOOST_TEST(row.at(0).get<std::size_t>() == id);
            BOOST_TEST(row.at(1).is_number_unsigned());
            BOOST_TEST(row.at(3) == miaow::task_status_to_string(result(id).status_));
            const miaow::TaskTiming& timing = record.task_timings_.at(id);
            BOOST_TEST(row.at(4) == (timing.dispatch_offset_ns_ < 0
                                         ? nlohmann::json(nullptr)
                                         : nlohmann::json(timing.dispatch_offset_ns_)));
            BOOST_TEST(row.at(5) == (timing.completion_offset_ns_ < 0
                                         ? nlohmann::json(nullptr)
                                         : nlohmann::json(timing.completion_offset_ns_)));
            if (result(id).status_ == miaow::TaskStatus::failed) {
                failures.push_back({{"task_id", id}, {"message", result(id).message_}});
            }
            if (!result(id).artifact_paths_.empty()) {
                artifacts.push_back(
                    {{"task_id", id}, {"paths", result(id).artifact_paths_}});
            }
        }
        BOOST_TEST(job_record.at("failures") == failures);
        BOOST_TEST(job_record.at("artifacts") == artifacts);
        BOOST_TEST(job_record.at("summary").at("failed_task_count") == failures.size());
    }

    const miaow::TaskResult& result(std::size_t id) const {
        return *record.task_results_.at(id);
    }

    std::string operation(std::size_t id) const {
        const auto& profile = job_record.at("profile");
        return profile.at("operations")
            .at(profile.at("tasks").at(id).at(2).get<std::size_t>())
            .get<std::string>();
    }

    std::size_t unit_id(std::size_t id) const {
        return job_record.at("profile").at("tasks").at(id).at(1).get<std::size_t>();
    }

    const nlohmann::json& unit(std::size_t id) const {
        return job_record.at("profile").at("units").at(unit_id(id));
    }

    bool dispatched(std::size_t id) const {
        return record.task_timings_.at(id).dispatch_offset_ns_ >= 0;
    }

    void check_successful_tasks() const {
        for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
            BOOST_TEST_CONTEXT("task " << id) {
                BOOST_TEST((result(id).status_ == miaow::TaskStatus::succeeded));
                BOOST_TEST(dispatched(id));
            }
        }
    }

    void check_updates(std::size_t expected) const {
        for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
            BOOST_TEST_CONTEXT("task " << id) {
                const std::string& message = result(id).message_;
                constexpr std::string_view count_label = "updates: ";
                const std::size_t count = message.find(count_label);
                BOOST_REQUIRE_MESSAGE(count != std::string::npos, "missing update count");
                BOOST_TEST(std::stoull(message.substr(count + count_label.size())) ==
                           expected);
            }
        }
    }

    // Preserve callback grouping, exactly-once delivery, and admission observations
    void check_lifecycle_observations(std::int64_t expected_unit_failure_count) const {
        const auto& output = job_record.at("output");
        BOOST_TEST(output.at("rejected_closed_unit").get<bool>());
        std::vector<std::size_t> reported_ids;
        std::int64_t unit_failure_count{0};
        nlohmann::json callbacks = nlohmann::json::array();
        for (const auto& callback : output.at("callbacks")) {
            std::vector<std::size_t> task_ids;
            for (const auto& id : callback.at("task_ids")) {
                task_ids.push_back(id.get<std::size_t>());
            }
            std::sort(task_ids.begin(), task_ids.end());
            reported_ids.insert(reported_ids.end(), task_ids.begin(), task_ids.end());
            unit_failure_count += callback.at("unit_failure_count").get<std::int64_t>();
            callbacks.push_back(
                {{"task_ids", task_ids},
                 {"unit_failure_count", callback.at("unit_failure_count")}});
        }
        BOOST_TEST(unit_failure_count == expected_unit_failure_count);
        std::sort(reported_ids.begin(), reported_ids.end());
        BOOST_REQUIRE_EQUAL(reported_ids.size(), record.task_results_.size());
        for (std::size_t id = 0; id < reported_ids.size(); ++id) {
            BOOST_TEST(reported_ids.at(id) == id);
        }
        if (test_case.value("input", nlohmann::json::object())
                .value("task_generation_policy", "asynchronous") == "batch_synchronous") {
            BOOST_TEST(callbacks == test_case.at("expected_callbacks"));
        }
    }

    miaow::JobRecord record;
    nlohmann::json job_record;
    nlohmann::json test_case = nlohmann::json::object();
    std::filesystem::path record_path;
    std::string execution_error;
};

std::string job_summary(const miaow::JobRecord& record) {
    std::ostringstream output;
    boost::io::ios_rdbuf_saver restore_output{std::cout, output.rdbuf()};
    miaow::print_job_summary(record);
    return output.str();
}

BOOST_FIXTURE_TEST_CASE(hello, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 2);
    BOOST_TEST(result(0).message_ == "hello");
    BOOST_TEST(result(1).message_ == "hello again");
    BOOST_TEST(unit(0).at("label") == "greeting");
    BOOST_TEST(unit(1).at("label") == "message_1");

    // Check summaries with deterministic timings and both terminal statuses
    miaow::JobRecord summary_record = record;
    summary_record.task_timings_ = {{0, 1000000000}, {1000000000, 3000000000}};
    summary_record.run_time_ = std::chrono::seconds{4};
    BOOST_TEST(miaow::all_tasks_succeeded(summary_record));
    summary_record.task_results_[1]->status_ = miaow::TaskStatus::failed;
    summary_record.task_results_[1]->message_.clear();
    BOOST_TEST(!miaow::all_tasks_succeeded(summary_record));
    const std::string expected =
        "\n=== MIAOW Job Summary "
        "==========================================================\n"
        "job_id: 0\n"
        "tasks:\n"
        "  [task_0] succeeded (1.000000 s): hello\n"
        "  [task_1] failed (2.000000 s)\n"
        "total_time: 4.000000\n"
        "concurrency: 0.750000\n"
        "================================================================================"
        "\n";
    BOOST_TEST(job_summary(summary_record) == expected);

    // Keep successful results on both sides of the missing entry
    summary_record.task_results_[1] = record.task_results_[1];
    summary_record.task_results_.insert(summary_record.task_results_.begin() + 1,
                                        std::nullopt);
    summary_record.task_timings_.insert(summary_record.task_timings_.begin() + 1,
                                        miaow::TaskTiming{});
    BOOST_TEST(!miaow::all_tasks_succeeded(summary_record));
    const std::string tasks = "tasks:\n"
                              "  [task_0] succeeded (1.000000 s): hello\n"
                              "  [task_1] result unavailable\n"
                              "  [task_2] succeeded (2.000000 s): hello again\n";
    BOOST_TEST(job_summary(summary_record).find(tasks) != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(hello_empty, JobRecordChecker) {
    check_successful_tasks();
    BOOST_TEST(record.task_results_.empty());
    BOOST_TEST(miaow::all_tasks_succeeded(record));

    // Check a copied record containing one missing result
    miaow::JobRecord missing_record = record;
    missing_record.task_results_.push_back(std::nullopt);
    BOOST_TEST(!miaow::all_tasks_succeeded(missing_record));
    BOOST_TEST(
        job_summary(missing_record).find("tasks:\n  [task_0] result unavailable\n") !=
        std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(heat, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 1);
    check_updates(120);
}

BOOST_FIXTURE_TEST_CASE(bmi_operations, JobRecordChecker) {
    check_successful_tasks();
    const std::array operations{"initialize",       "get_current_time", "update",
                                "get_current_time", "update",           "get_current_time",
                                "finalize"};
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), operations.size());
    for (std::size_t id = 0; id < operations.size(); ++id) {
        BOOST_TEST(operation(id) == operations[id]);
        BOOST_TEST(unit_id(id) == unit_id(0));
    }
    const std::array times{0.0, 1.0 / 6.0, 1.0 / 3.0};
    for (std::size_t index = 0; index < times.size(); ++index) {
        BOOST_TEST(std::abs(caf::get<double>(result(2 * index + 1).output_) -
                            times[index]) < 1e-14);
    }
}

// Complete snapshots follow both model chains without mixing time steps
BOOST_FIXTURE_TEST_CASE(model_coupling, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 22);
    std::array<std::array<double, 25>, 5> fields{};
    fields[0][12] = 1.0;
    for (std::size_t step = 1; step < fields.size(); ++step) {
        for (std::size_t row = 1; row < 4; ++row) {
            for (std::size_t column = 1; column < 4; ++column) {
                const std::size_t index = row * 5 + column;
                const auto& previous = fields[step - 1];
                fields[step][index] = 0.5 * previous[index] +
                                      0.125 * (previous[index - 1] + previous[index + 1] +
                                               previous[index - 5] + previous[index + 5]);
            }
        }
    }
    for (std::size_t step = 0; step < 3; ++step) {
        const std::array operations{"update", "get_value", "set_value", "update",
                                    "get_value"};
        for (std::size_t index = 0; index < operations.size(); ++index) {
            const std::size_t id = 3 + 5 * step + index;
            BOOST_TEST(operation(id) == operations[index]);
            BOOST_TEST(unit(id).at("label") == (index < 2 ? "source" : "receiver"));
        }
        for (std::size_t model = 0; model < 2; ++model) {
            const auto& values =
                caf::get<caf::config_value::list>(result(4 + 5 * step + 3 * model).output_);
            BOOST_REQUIRE_EQUAL(values.size(), 25);
            for (std::size_t index = 0; index < values.size(); ++index) {
                BOOST_TEST(std::abs(caf::get<double>(values[index]) -
                                    fields[step + 1 + model][index]) < 1e-15);
            }
        }
    }
    BOOST_TEST(caf::get<double>(result(18).output_) == 0.75);
    BOOST_TEST(caf::get<double>(result(20).output_) == 0.75);
    BOOST_TEST(operation(19) == "finalize");
    BOOST_TEST(operation(21) == "finalize");
}

// Preserve double snapshots and the Fortran integer variable across all five paths
BOOST_FIXTURE_TEST_CASE(bmi_values, JobRecordChecker) {
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 30);
    std::size_t field_reads = 0;
    std::size_t integer_reads = 0;
    std::size_t rejected_writes = 0;
    std::size_t blocked_reads = 0;
    std::size_t finalizations = 0;
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        const std::string name = operation(id);
        const miaow::TaskResult& task_result = result(id);
        if (task_result.status_ == miaow::TaskStatus::failed) {
            if (name == "set_value") {
                ++rejected_writes;
                BOOST_TEST(dispatched(id));
                BOOST_TEST(
                    (task_result.message_.find("value count") != std::string::npos ||
                     task_result.message_.find("native type range") != std::string::npos));
            } else {
                ++blocked_reads;
                BOOST_TEST(name == "get_value");
                BOOST_TEST(!dispatched(id));
                BOOST_TEST(task_result.message_.find("Dependency error") == 0);
            }
            continue;
        }
        if (name == "get_value") {
            const auto& values = caf::get<caf::config_value::list>(task_result.output_);
            if (values.size() == 1) {
                ++integer_reads;
                BOOST_TEST((caf::get_if<std::int64_t>(&values[0]) != nullptr));
                BOOST_TEST(caf::get<std::int64_t>(values[0]) == 42);
            } else {
                ++field_reads;
                BOOST_REQUIRE_EQUAL(values.size(), 25);
                const bool fortran = unit(id).at("label").get<std::string>().find(
                                         "fortran") != std::string::npos;
                for (std::size_t index = 0; index < values.size(); ++index) {
                    const double source = 0.1 * static_cast<double>(index);
                    const double expected =
                        fortran ? static_cast<double>(static_cast<float>(source)) : source;
                    BOOST_TEST(caf::get<double>(values[index]) == expected);
                }
            }
        } else if (name == "finalize") {
            ++finalizations;
        }
    }
    BOOST_TEST(field_reads == 5);
    BOOST_TEST(integer_reads == 2);
    BOOST_TEST(rejected_writes == 3);
    BOOST_TEST(blocked_reads == 3);
    BOOST_TEST(finalizations == 5);
}

BOOST_FIXTURE_TEST_CASE(simulation_unit_task_dependencies, JobRecordChecker) {
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 11);
    const bool concurrent = test_case.at("max_workers").get<std::size_t>() > 1;
    const std::array<std::int64_t, 11> expected =
        concurrent ? std::array<std::int64_t, 11>{1, 10, 3, 2, 13, 0, 0, 0, 1, 4, 17}
                   : std::array<std::int64_t, 11>{1, 10, 2, 3, 12, 0, 0, 0, 1, 4, 16};
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        const bool failed = id >= 5 && id <= 7;
        BOOST_TEST(miaow::task_status_to_string(result(id).status_) ==
                   (failed ? "failed" : "succeeded"));
        BOOST_TEST(!dispatched(id) == (id == 6 || id == 7));
        if (!failed) {
            BOOST_TEST(caf::get<std::int64_t>(result(id).output_) == expected[id]);
        }
    }
    BOOST_TEST(result(6).message_ == "Dependency error: prerequisite task 5 failed");
    BOOST_TEST(result(7).message_ == "Dependency error: prerequisite task 6 failed");
    std::vector<std::size_t> reported;
    for (const auto& callback : job_record.at("output")) {
        BOOST_TEST(callback.at("unit_failure_count").get<int>() == 0);
        for (const auto& id : callback.at("task_ids")) {
            reported.push_back(id.get<std::size_t>());
        }
    }
    if (concurrent) {
        BOOST_TEST((std::find(reported.begin(), reported.end(), 3) <
                    std::find(reported.begin(), reported.end(), 2)));
    }
    std::sort(reported.begin(), reported.end());
    BOOST_REQUIRE_EQUAL(reported.size(), record.task_results_.size());
    for (std::size_t id = 0; id < reported.size(); ++id) {
        BOOST_TEST(reported[id] == id);
    }
}

BOOST_FIXTURE_TEST_CASE(bmi_lifecycle, JobRecordChecker) {
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 11);
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        const bool failed = id == 0 || id == 3 || id == 4 || id == 7;
        BOOST_TEST(miaow::task_status_to_string(result(id).status_) ==
                   (failed ? "failed" : "succeeded"));
        BOOST_TEST(dispatched(id));
        BOOST_TEST(unit_id(id) == unit_id(0));
    }
    BOOST_TEST(std::abs(caf::get<double>(result(5).output_) - 0.25) < 1e-14);
    BOOST_TEST(caf::get<double>(result(9).output_) == 0.0);
}

BOOST_FIXTURE_TEST_CASE(bmi_failure_recovery, JobRecordChecker) {
    const std::string name = test_case.at("input").at("operation").get<std::string>();
    const std::size_t failure_task = name == "initialize" ? 0 : 1;
    const bool model_released = name == "initialize" || name == "finalize";
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), failure_task + 7);
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        const bool failed = id == failure_task || id == failure_task + 1 ||
                            id == failure_task + 2 ||
                            (id == failure_task + 3 && model_released);
        BOOST_TEST(miaow::task_status_to_string(result(id).status_) ==
                   (failed ? "failed" : "succeeded"));
        BOOST_TEST(dispatched(id));
        BOOST_TEST(unit_id(id) == unit_id(0));
    }
    BOOST_TEST(result(failure_task).message_ ==
               test_case.at("failure_message").get<std::string>());
    for (std::size_t id : {failure_task + 1, failure_task + 2}) {
        BOOST_TEST(result(id).message_ ==
                   (model_released
                        ? "BMI error: model is not initialized"
                        : "BMI error: model must be finalized after a failed BMI call"));
    }
    if (model_released) {
        BOOST_TEST(result(failure_task + 3).message_ ==
                   "BMI error: model is not initialized");
    }
    BOOST_TEST(caf::get<double>(result(failure_task + 6).output_) == 0.25);
    const auto& models = job_record.at("output");
    BOOST_REQUIRE(models.is_array());
    BOOST_REQUIRE_EQUAL(models.size(), 2);
    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto& counts = models.at(index);
        BOOST_TEST(counts.at("finalizations") ==
                   (index == 0 && name == "initialize" ? 0 : 1));
        BOOST_TEST(counts.at("destructions") == 1);
        BOOST_TEST(counts.at("exception_destroyed_before_model").get<bool>() ==
                   (index == 0 && model_released));
        BOOST_TEST(counts.at("updates") == (index == 1 || name == "update" ? 1 : 0));
        BOOST_TEST(counts.at("queries") ==
                   (index == 1 || name == "get_current_time" ? 1 : 0));
    }
}

BOOST_FIXTURE_TEST_CASE(dynamic_bmi, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 4);
    BOOST_TEST(unit(0).at("task_executor_id") != unit(2).at("task_executor_id"));
    BOOST_TEST(unit(1).at("task_executor_id") != unit(3).at("task_executor_id"));
    check_updates(40);
}

BOOST_FIXTURE_TEST_CASE(dynamic_bmi_zero_duration, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 4);
    check_updates(0);
}

BOOST_FIXTURE_TEST_CASE(bmi_repeated_run, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 10);
    check_updates(40);
    for (std::size_t id = 0; id < record.task_results_.size(); id += 2) {
        BOOST_TEST(unit_id(id) == unit_id(id + 1));
    }
}

BOOST_FIXTURE_TEST_CASE(simulation_unit_removal_with_queued_work, JobRecordChecker) {
    check_successful_tasks();
    check_lifecycle_observations(0);
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 4);
    const std::array expected_counts{1, 2, 1, 2};
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        BOOST_TEST(caf::get<std::int64_t>(result(id).output_) == expected_counts.at(id));
    }
    BOOST_TEST(unit_id(0) == unit_id(1));
    BOOST_TEST(unit_id(2) == unit_id(3));
    BOOST_TEST(unit_id(0) != unit_id(2));
}

BOOST_FIXTURE_TEST_CASE(simulation_unit_replacement_after_failure, JobRecordChecker) {
    check_lifecycle_observations(1);
    const auto& output = job_record.at("output");
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 6);
    for (std::size_t id = 0; id < 2; ++id) {
        BOOST_TEST((result(id).status_ == miaow::TaskStatus::failed));
        BOOST_TEST(result(id).message_.find("intentional executor construction failure") !=
                   std::string::npos);
    }
    BOOST_TEST(dispatched(0));
    BOOST_TEST(!dispatched(1));
    BOOST_TEST(output.at("failed_unit_matches_target").get<bool>());
    BOOST_TEST(output.at("failed_tasks_reported_with_unit_failure") == 2);
    const std::array expected_counts{1, 1, 2, 2};
    for (std::size_t id = 2; id < record.task_results_.size(); ++id) {
        BOOST_TEST((result(id).status_ == miaow::TaskStatus::succeeded));
        BOOST_TEST(caf::get<std::int64_t>(result(id).output_) ==
                   expected_counts.at(id - 2));
    }
    BOOST_TEST(unit_id(0) == unit_id(1));
    BOOST_TEST(unit_id(2) == unit_id(5));
    BOOST_TEST(unit_id(3) == unit_id(4));
    BOOST_TEST(unit_id(0) != unit_id(3));
    BOOST_TEST(unit_id(2) != unit_id(3));
    BOOST_TEST(unit(0).at("label") == unit(3).at("label"));
    BOOST_TEST(output.at("replacement_has_new_id").get<bool>());
}

BOOST_FIXTURE_TEST_CASE(subprocess_shutdown, JobRecordChecker) {
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), 4);
    check_lifecycle_observations(1);
    const bool cleanup_finished = test_case.at("cleanup_finished").get<bool>();
    for (std::size_t id : {1, 2}) {
        BOOST_TEST((result(id).status_ == miaow::TaskStatus::failed));
        BOOST_TEST(result(id).message_.find("request_timeout") != std::string::npos);
        const std::string termination =
            cleanup_finished
                ? "subprocess exited with code 0"
                : "subprocess was terminated by signal " + std::to_string(SIGKILL);
        BOOST_TEST(result(id).message_.find(termination) != std::string::npos);
        BOOST_TEST(unit_id(id) == unit_id(0));
    }
    BOOST_TEST(!dispatched(2));
    BOOST_TEST((result(0).status_ == miaow::TaskStatus::succeeded));
    BOOST_TEST(caf::get<std::int64_t>(result(0).output_) > 0);
    BOOST_TEST((result(3).status_ == miaow::TaskStatus::succeeded));
    BOOST_TEST(caf::get<std::int64_t>(result(3).output_) == 1);
    BOOST_TEST(unit_id(3) != unit_id(0));
    const auto& output = job_record.at("output");
    BOOST_TEST(output.at("child_reaped_before_callback").get<bool>());
    BOOST_TEST(output.at("other_task_waited_for_shutdown").get<bool>());
    const auto directory = record_path.parent_path() / "results";
    BOOST_TEST(std::filesystem::exists(directory / "call_finished"));
    BOOST_TEST(std::filesystem::exists(directory / "cleanup_started"));
    BOOST_TEST(std::filesystem::exists(directory / "cleanup_finished") == cleanup_finished);
}

// Heron estimates and residuals are checked directly at their original precision
BOOST_FIXTURE_TEST_CASE(controller, JobRecordChecker) {
    check_successful_tasks();
    const std::array estimates{1.5, 17.0 / 12.0, 577.0 / 408.0};
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), estimates.size());
    for (std::size_t id = 0; id < estimates.size(); ++id) {
        const auto& output = caf::get<caf::config_value::dictionary>(result(id).output_);
        const double estimate = caf::get<double>(output, "estimate");
        const double residual = caf::get<double>(output, "residual");
        BOOST_TEST(std::abs(estimate - estimates[id]) < 1e-14);
        BOOST_TEST(std::abs(residual - std::abs(estimate * estimate - 2.0)) < 1e-14);
        if (id + 1 < estimates.size()) {
            BOOST_TEST(residual > 1e-5);
        } else {
            BOOST_TEST(residual <= 1e-5);
            BOOST_TEST(std::abs(estimate - std::sqrt(2.0)) < 1e-5);
        }
    }
}

BOOST_FIXTURE_TEST_CASE(expected_failure, JobRecordChecker) {
    // Exercise C++ worker-limit validation alongside the CLI rejection
    if (test_case.contains("max_workers") && test_case.at("max_workers") == 0) {
        BOOST_CHECK_THROW(miaow::RuntimeOptions{0}, std::invalid_argument);
        miaow::RuntimeOptions options{2};
        BOOST_CHECK_THROW(options.set_max_workers(0), std::invalid_argument);
        BOOST_TEST(options.max_workers() == 2);
    }
    if (!test_case.contains("tasks")) {
        BOOST_TEST(!std::filesystem::exists(record_path));
        return;
    }
    const auto& expected = test_case.at("tasks");
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), expected.size());
    for (std::size_t id = 0; id < expected.size(); ++id) {
        const auto& reference = expected.at(id);
        BOOST_TEST(miaow::task_status_to_string(result(id).status_) ==
                   reference.at("status").get<std::string>());
        BOOST_TEST(result(id).message_.find(reference.at("message").get<std::string>()) !=
                   std::string::npos);
        BOOST_TEST(dispatched(id) == reference.value("dispatched", true));
    }
}

BOOST_FIXTURE_TEST_CASE(controller_small_values, JobRecordChecker) {
    check_successful_tasks();
    const std::array estimates{1.5e-8, (17.0 / 12.0) * 1e-8, (577.0 / 408.0) * 1e-8};
    BOOST_REQUIRE_EQUAL(record.task_results_.size(), estimates.size());
    for (std::size_t id = 0; id < estimates.size(); ++id) {
        const auto& output = caf::get<caf::config_value::dictionary>(result(id).output_);
        const double estimate = caf::get<double>(output, "estimate");
        const double residual = caf::get<double>(output, "residual");
        BOOST_TEST(std::abs(estimate / estimates[id] - 1.0) < 1e-14);
        BOOST_TEST(std::abs(residual - std::abs(estimate * estimate - 2e-16)) < 1e-30);
        if (id + 1 < record.task_results_.size()) {
            BOOST_TEST(residual > 1e-21);
        } else {
            BOOST_TEST(residual > 0.0);
            BOOST_TEST(residual <= 1e-21);
        }
    }
}

} // namespace
