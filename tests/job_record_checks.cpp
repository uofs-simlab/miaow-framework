// This file defines the Boost regression checks used by the miaow_job_record_checks
// executable.
//
// tests/CMakeLists.txt builds it with:
//   add_executable(miaow_job_record_checks job_record_checks.cpp)
//
// Default-build invocation from the repository root:
//   build/tests/miaow_job_record_checks --run_test=<test> -- <job_record.json> [case.json]

#define BOOST_TEST_MODULE miaow_job_record_checks
#include <boost/test/unit_test.hpp>

#include <nlohmann/json.hpp>

#include "core/build_info.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Shared saved-record data and checks for successful and failed tasks
struct JobRecordChecker {
    // Load the record, index tasks by ID, and validate job and unit identities
    JobRecordChecker() {
        const auto& arguments = boost::unit_test::framework::master_test_suite();
        // Cases may also pass the JSON used to configure the driver
        BOOST_REQUIRE(arguments.argc == 2 || arguments.argc == 3);
        std::ifstream job_record_input{arguments.argv[1]};
        job_record = nlohmann::json::parse(job_record_input);
        BOOST_TEST(job_record.at("job_submission_id").is_number_unsigned());

        // Records identify the library build that produced them
        BOOST_TEST(job_record.at("miaow_version").get<std::string>() ==
                   miaow::core::build_version);
        BOOST_REQUIRE(job_record.at("tasks").is_array());

        // Compare task identities independently of parallel completion order
        for (const auto& task : job_record.at("tasks")) {
            BOOST_REQUIRE(task.at("task_id").is_number_unsigned());
            const std::size_t id = task.at("task_id").get<std::size_t>();
            // Task IDs must be unique so no task record is lost when building the lookup
            BOOST_REQUIRE_MESSAGE(tasks.emplace(id, task).second, "duplicate task result");

            // Targets retain the job's creation identity and their declaration index
            BOOST_REQUIRE(task.at("unit").at("job_creation_id").is_number_unsigned());
            BOOST_TEST(task.at("unit").at("job_creation_id").get<std::uint64_t>() > 0);
            BOOST_TEST(task.at("unit").at("index").is_number_unsigned());
        }
    }

    // Require every saved task to have succeeded and include its dispatch offset
    void check_successful_tasks() const {
        for (const auto& [id, task] : tasks) {
            BOOST_TEST_CONTEXT("task " << id) {
                BOOST_TEST(task.at("result").at("status").get<std::string>() ==
                           "succeeded");
                BOOST_TEST(!task.at("dispatch_offset_from_job_start").is_null());
            }
        }
    }

    // Require each BMI result message to report the expected number of updates
    void check_updates(std::size_t expected) const {
        for (std::size_t id = 0; id < tasks.size(); ++id) {
            BOOST_TEST_CONTEXT("task " << id) {
                const std::string message =
                    tasks.at(id).at("result").at("message").get<std::string>();
                constexpr std::string_view count_label = "updates: ";
                const std::size_t count = message.find(count_label);
                // The BMI task result message must report an update count for comparison
                BOOST_REQUIRE_MESSAGE(count != std::string::npos, "missing update count");

                // The model must take the expected number of steps to reach its end time
                BOOST_TEST(std::stoull(message.substr(count + count_label.size())) ==
                           expected);
            }
        }
    }

    nlohmann::json job_record;
    std::map<std::size_t, nlohmann::json> tasks;
};

// Check old-handle rejection, exactly-once delivery, and total callback unit failures
void check_lifecycle_observations(const nlohmann::json& job_record,
                                  std::int64_t expected_unit_failure_count) {
    const auto& tasks = job_record.at("tasks");
    const auto& output = job_record.at("output");
    BOOST_TEST(output.at("rejected_old_handle").at("value").get<bool>());

    // Retain callback order but ignore completion order within each callback
    std::vector<std::size_t> reported_ids;
    std::int64_t unit_failure_count{0};
    nlohmann::json callbacks = nlohmann::json::array();
    for (const auto& callback : output.at("callbacks").at("value")) {
        const auto& observation = callback.at("value");
        std::vector<std::size_t> task_ids;
        for (const auto& id : observation.at("task_ids").at("value")) {
            task_ids.push_back(id.at("value").get<std::size_t>());
        }
        std::sort(task_ids.begin(), task_ids.end());
        reported_ids.insert(reported_ids.end(), task_ids.begin(), task_ids.end());
        unit_failure_count +=
            observation.at("unit_failure_count").at("value").get<std::int64_t>();
        callbacks.push_back(
            {{"task_ids", task_ids},
             {"unit_failure_count", observation.at("unit_failure_count").at("value")}});
    }
    BOOST_TEST(unit_failure_count == expected_unit_failure_count);

    // Every task must reach the controller exactly once, regardless of callback order
    std::sort(reported_ids.begin(), reported_ids.end());
    BOOST_REQUIRE_EQUAL(reported_ids.size(), tasks.size());
    for (std::size_t id = 0; id < tasks.size(); ++id) {
        BOOST_TEST(reported_ids.at(id) == id);
        BOOST_TEST(tasks.at(id).at("task_id").get<std::size_t>() == id);
    }

    // Batch cases require complete groups, with unit loss deferred to the batch boundary
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    if (arguments.argc == 3) {
        std::ifstream case_input{arguments.argv[2]};
        const nlohmann::json test_case = nlohmann::json::parse(case_input);
        if (test_case.at("input").value("task_generation_policy", "asynchronous") ==
            "batch_synchronous") {
            BOOST_TEST(callbacks == test_case.at("expected_callbacks"));
        }
    }
}

} // namespace

// CTest runs each example through tests/run_example.cmake, then selects a checker here
// with --run_test=<REGRESSION> to inspect the saved job record.

// Registered in examples/00_hello/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(hello, JobRecordChecker) {
    check_successful_tasks();
    // Both submitted greetings must have a saved task record
    BOOST_REQUIRE_EQUAL(tasks.size(), 2);

    // Each task result message must match the message supplied to that particular task
    BOOST_TEST(tasks.at(0).at("result").at("message").get<std::string>() == "hello");
    BOOST_TEST(tasks.at(1).at("result").at("message").get<std::string>() == "hello again");

    // User-supplied metadata must survive into the saved task record
    BOOST_TEST(tasks.at(0).at("metadata").at("feature_id").get<std::string>() ==
               "greeting");

    // Unit labels and task IDs identify each operation's artifacts
    BOOST_TEST(tasks.at(0).at("task_name").get<std::string>() == "greeting_task_0");
    BOOST_TEST(tasks.at(1).at("task_name").get<std::string>() == "message_1_task_1");
}

// An empty submission must still produce a successful, empty job record
BOOST_FIXTURE_TEST_CASE(hello_empty, JobRecordChecker) {
    check_successful_tasks();
    BOOST_TEST(tasks.empty());
}

// Registered in examples/01_heat/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(heat, JobRecordChecker) {
    check_successful_tasks();
    // The single statically linked BMI task must have a saved task record
    BOOST_REQUIRE_EQUAL(tasks.size(), 1);

    // With alpha = 1.5, dt = 1 / (4 * alpha) = 1/6; reaching time 20 takes 120 steps
    check_updates(120);
}

// Registered in examples/03_dynamic_bmi/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(dynamic_bmi, JobRecordChecker) {
    check_successful_tasks();
    // Each of the C++ and Fortran plugins runs in-process and in a subprocess
    BOOST_REQUIRE_EQUAL(tasks.size(), 4);

    // The C++ tasks must use distinct registered executors for the two execution modes
    BOOST_TEST(tasks.at(0).at("task_executor_id").get<std::size_t>() !=
               tasks.at(2).at("task_executor_id").get<std::size_t>());

    // The Fortran tasks must also use distinct executors for the two execution modes
    BOOST_TEST(tasks.at(1).at("task_executor_id").get<std::size_t>() !=
               tasks.at(3).at("task_executor_id").get<std::size_t>());

    // Both models use alpha = 1, so dt = 0.25; all four runs need 40 steps to time 10
    check_updates(40);
}

// Both plugin languages and execution modes must accept a model already at its end time
BOOST_FIXTURE_TEST_CASE(dynamic_bmi_zero_duration, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(tasks.size(), 4);
    check_updates(0);
}

// Each BMI execution path must complete two fresh model runs on the same unit
BOOST_FIXTURE_TEST_CASE(bmi_repeated_run, JobRecordChecker) {
    check_successful_tasks();
    BOOST_REQUIRE_EQUAL(tasks.size(), 10);
    check_updates(40);

    for (std::size_t id = 0; id < tasks.size(); id += 2) {
        BOOST_TEST_CONTEXT("tasks " << id << " and " << id + 1) {
            BOOST_TEST(tasks.at(id).at("unit") == tasks.at(id + 1).at("unit"));
        }
    }
}

// Removing a unit before dispatch must drain its queue and reject subsequent tasks
BOOST_FIXTURE_TEST_CASE(simulation_unit_removal_with_queued_work, JobRecordChecker) {
    check_successful_tasks();
    check_lifecycle_observations(job_record, 0);
    BOOST_REQUIRE_EQUAL(tasks.size(), 4);
    const std::array expected_counts{1, 2, 1, 2};
    for (std::size_t id = 0; id < tasks.size(); ++id) {
        BOOST_TEST_CONTEXT("task " << id) {
            BOOST_TEST(tasks.at(id).at("result").at("output").get<std::int64_t>() ==
                       expected_counts.at(id));
        }
    }

    // Accepted work retains the removed unit's state; the other handle remains usable
    BOOST_TEST(tasks.at(0).at("unit") == tasks.at(1).at("unit"));
    BOOST_TEST(tasks.at(2).at("unit") == tasks.at(3).at("unit"));
    BOOST_TEST(tasks.at(0).at("unit") != tasks.at(2).at("unit"));
}

// Unit loss must report active and queued tasks together and permit explicit replacement
BOOST_FIXTURE_TEST_CASE(simulation_unit_replacement_after_failure, JobRecordChecker) {
    check_lifecycle_observations(job_record, 1);
    const auto& output = job_record.at("output");
    BOOST_REQUIRE_EQUAL(tasks.size(), 6);
    for (std::size_t id = 0; id < 2; ++id) {
        BOOST_TEST_CONTEXT("failed task " << id) {
            BOOST_TEST(tasks.at(id).at("result").at("status").get<std::string>() ==
                       "failed");
            const std::string message =
                tasks.at(id).at("result").at("message").get<std::string>();
            BOOST_TEST(message.find("intentional executor construction failure") !=
                       std::string::npos);
        }
    }

    // The active request fails during activation; its queued successor never dispatches
    BOOST_TEST(!tasks.at(0).at("dispatch_offset_from_job_start").is_null());
    BOOST_TEST(tasks.at(1).at("dispatch_offset_from_job_start").is_null());
    BOOST_TEST(tasks.at(1).at("run_time").get<std::string>() == "0s");
    BOOST_TEST(output.at("failed_unit_matches_target").at("value").get<bool>());
    BOOST_TEST(output.at("failed_tasks_reported_with_unit_failure").at("value") == 2);

    // Replacement state starts fresh and persists; the other unit keeps its own state
    const std::array expected_counts{1, 1, 2, 2};
    for (std::size_t id = 2; id < tasks.size(); ++id) {
        BOOST_TEST_CONTEXT("successful task " << id) {
            BOOST_TEST(tasks.at(id).at("result").at("status").get<std::string>() ==
                       "succeeded");
            BOOST_TEST(tasks.at(id).at("result").at("output").get<std::int64_t>() ==
                       expected_counts.at(id - 2));
        }
    }
    BOOST_TEST(tasks.at(0).at("unit") == tasks.at(1).at("unit"));
    BOOST_TEST(tasks.at(2).at("unit") == tasks.at(5).at("unit"));
    BOOST_TEST(tasks.at(3).at("unit") == tasks.at(4).at("unit"));
    BOOST_TEST(tasks.at(0).at("unit") != tasks.at(3).at("unit"));
    BOOST_TEST(tasks.at(2).at("unit") != tasks.at(3).at("unit"));
    BOOST_TEST(tasks.at(0).at("metadata").at("feature_id") ==
               tasks.at(3).at("metadata").at("feature_id"));
    BOOST_TEST(output.at("replacement_has_new_handle").at("value").get<bool>());
}

// Registered in examples/07_job_controller/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(controller, JobRecordChecker) {
    check_successful_tasks();
    // Heron iteration x_(n+1) = (x_n + 2 / x_n) / 2, starting at x_0 = 1
    const std::array estimates{1.5, 17.0 / 12.0, 577.0 / 408.0};

    // The controller must generate exactly three iterations, stopping at convergence
    BOOST_REQUIRE_EQUAL(tasks.size(), estimates.size());
    for (std::size_t id = 0; id < estimates.size(); ++id) {
        BOOST_TEST_CONTEXT("iteration " << id + 1) {
            const auto& output = tasks.at(id).at("result").at("output");
            const double estimate = output.at("estimate").at("value").get<double>();
            const double residual = output.at("residual").at("value").get<double>();

            // Check the expected iteration sequence, allowing floating-point rounding
            BOOST_TEST(std::abs(estimate - estimates[id]) < 1e-14);

            // The recorded residual must equal |x^2 - 2| for the recorded estimate
            BOOST_TEST(std::abs(residual - std::abs(estimate * estimate - 2.0)) < 1e-14);
            if (id + 1 < estimates.size()) {
                // Earlier iterations must still require another task
                BOOST_TEST(residual > 1e-5);
            } else {
                // The last iteration must satisfy the controller's stopping tolerance
                BOOST_TEST(residual <= 1e-5);

                // Its estimate must also approximate the positive square root to 1e-5
                BOOST_TEST(std::abs(estimate - std::sqrt(2.0)) < 1e-5);
            }
        }
    }
}

// Registered in the following examples:
//   examples/00_hello/CMakeLists.txt (case file: tests/cases/hello_*.json)
//   examples/03_dynamic_bmi/CMakeLists.txt (case file: tests/cases/dynamic_*.json)
//   examples/07_job_controller/CMakeLists.txt
//      (case file: tests/cases/controller_non_convergence.json)
BOOST_AUTO_TEST_CASE(expected_failure) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    // Arguments: miaow_job_record_checks executable path, job-record path, and case-file
    // path
    BOOST_REQUIRE_EQUAL(arguments.argc, 3);
    std::ifstream case_input{arguments.argv[2]};
    const nlohmann::json expected = nlohmann::json::parse(case_input);
    const std::filesystem::path record_path{arguments.argv[1]};
    if (!expected.contains("tasks")) {
        // These cases fail before a final job record can be saved
        BOOST_TEST(!std::filesystem::exists(record_path));
        return;
    }

    const JobRecordChecker checker;
    const auto& tasks = checker.job_record.at("tasks");
    const auto& expected_tasks = expected.at("tasks");

    // Every expected task, including failed tasks, must have exactly one saved task record
    BOOST_REQUIRE_EQUAL(tasks.size(), expected_tasks.size());
    for (std::size_t id = 0; id < tasks.size(); ++id) {
        BOOST_TEST_CONTEXT("task " << id) {
            const auto& task = tasks.at(id);
            const auto& reference = expected_tasks.at(id);
            const auto& result = task.at("result");

            // Task records must remain in submission order regardless of completion order
            BOOST_TEST(task.at("task_id").get<std::size_t>() == id);

            // Check each task's expected outcome, including success after another fails
            BOOST_TEST(result.at("status").get<std::string>() ==
                       reference.at("status").get<std::string>());

            // Match the expected message fragment, allowing extra diagnostic context
            const std::string message = result.at("message").get<std::string>();
            BOOST_TEST(message.find(reference.at("message").get<std::string>()) !=
                       std::string::npos);
            const bool dispatched = reference.value("dispatched", true);

            // Dispatch timing must be present for dispatched tasks and null otherwise
            BOOST_TEST(task.at("dispatch_offset_from_job_start").is_null() == !dispatched);
            if (!dispatched) {
                // Tasks that never dispatched must have zero run time
                BOOST_TEST(task.at("run_time").get<std::string>() == "0s");
            }
        }
    }
}

// Registered in examples/07_job_controller/CMakeLists.txt
//      (case file: tests/cases/controller_small_values.json)
//
// The example computes sqrt(2e-16); this checker verifies that small estimates and
// residuals retain their precision in the saved job record.
BOOST_FIXTURE_TEST_CASE(controller_small_values, JobRecordChecker) {
    check_successful_tasks();
    // With radicand S = 2e-16 and x_0 = 1e-8, the expected sqrt(2) iterates are
    // scaled by 1e-8
    const std::array estimates{1.5e-8, (17.0 / 12.0) * 1e-8, (577.0 / 408.0) * 1e-8};

    // The smaller radicand and tolerance must still produce exactly three iterations
    BOOST_REQUIRE_EQUAL(tasks.size(), estimates.size());

    // Check each task's result against the expected scaled Heron's method iterates
    for (std::size_t id = 0; id < tasks.size(); ++id) {
        BOOST_TEST_CONTEXT("iteration " << id + 1) {
            const auto& result = tasks.at(id).at("result");

            const auto& output = result.at("output");
            const double estimate = output.at("estimate").at("value").get<double>();
            const double residual = output.at("residual").at("value").get<double>();

            // The expected iterates should match the scaled Heron's method values with
            // high precision, when considering relative error
            BOOST_TEST(std::abs(estimate / estimates[id] - 1.0) < 1e-14);

            // Preserve residual = |x^2 - S| through the serialized task output
            BOOST_TEST(std::abs(residual - std::abs(estimate * estimate - 2e-16)) < 1e-30);
            if (id + 1 < tasks.size()) {
                // Earlier residuals must remain above the configured stopping tolerance
                BOOST_TEST(residual > 1e-21);
            } else { // last result
                // A small, nonzero final residual must not be rounded to zero on disk
                BOOST_TEST(residual > 0.0);

                // The final residual must satisfy the stopping tolerance
                BOOST_TEST(residual <= 1e-21);
            }
        }
    }
}
