// This file defines the Boost regression checks used by the miaow_example_tests executable.
//
// tests/CMakeLists.txt builds it with:
//   add_executable(miaow_example_tests example_regression_checks.cpp)
//
// Default-build invocation from the repository root:
//   build/tests/miaow_example_tests --run_test=<test> -- <job_record.json> [case.json]

#define BOOST_TEST_MODULE miaow_examples
#include <boost/test/unit_test.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace {

// Shared fixture that loads and checks saved job records from successful example runs.
struct JobRecordChecker {
    JobRecordChecker() {
        const auto& arguments = boost::unit_test::framework::master_test_suite();
        // Arguments: miaow_example_tests executable path and saved job-record path
        BOOST_REQUIRE_EQUAL(arguments.argc, 2);
        std::ifstream job_record_input{arguments.argv[1]};
        const auto job_record = nlohmann::json::parse(job_record_input);

        // Compare task identities independently of parallel completion order
        for (const auto& task : job_record.at("tasks")) {
            const auto id = task.at("task_id").get<std::size_t>();
            // Task IDs must be unique so no task record is lost when building the lookup
            BOOST_REQUIRE_MESSAGE(tasks.emplace(id, task).second, "duplicate task result");

            // This fixture checks successful runs, so every recorded task must succeed
            BOOST_TEST(task.at("result").at("status").get<std::string>() == "succeeded");

            // CTest limits these jobs to two workers, with IDs starting at zero
            BOOST_TEST(task.at("worker_id").get<std::size_t>() < 2);

            // Executed tasks must include their dispatch offset from the job start
            BOOST_TEST(!task.at("dispatch_offset_from_job_start").is_null());
        }
    }

    void check_updates(std::size_t expected) const {
        for (std::size_t id = 0; id < tasks.size(); ++id) {
            BOOST_TEST_CONTEXT("task " << id) {
                const auto message =
                    tasks.at(id).at("result").at("message").get<std::string>();
                constexpr std::string_view count_label = "updates: ";
                const auto count = message.find(count_label);
                // The BMI task result message must report an update count for comparison
                BOOST_REQUIRE_MESSAGE(count != std::string::npos, "missing update count");

                // The model must take the expected number of steps to reach its end time
                BOOST_TEST(std::stoull(message.substr(count + count_label.size())) ==
                           expected);
            }
        }
    }

    std::map<std::size_t, nlohmann::json> tasks;
};

} // namespace

// Before these checks run, tests/run_example.cmake runs the configured example executable.
// BOOST_FIXTURE_TEST_CASE(name, fixture) registers a named Boost test whose generated class
// derives from the fixture. Boost constructs a fresh fixture before running the test body,
// which can access the fixture's members, including its map of saved task records.
// JobRecordChecker's constructor loads the job record and performs the shared checks;
// the body performs additional checks specific to that example.

// Each example's CMakeLists.txt registers CTest tests through miaow_add_example_test:
// TEST_NAME names the CTest test, EXECUTABLE_TARGET selects the example executable, and
// REGRESSION selects the Boost test in miaow_example_tests via --run_test=<REGRESSION>.
// Multiple CTest registrations may reuse a REGRESSION value. Each registration runs its
// own configured example, then invokes the selected checker on that example's output.
// BUILD_TESTING enables test registration; ctest executes the registered tests.

// Registered in examples/00_hello/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(hello, JobRecordChecker) {
    // Both submitted greetings must have a saved task record
    BOOST_REQUIRE_EQUAL(tasks.size(), 2);

    // Each task result message must match the message supplied to that particular task
    BOOST_TEST(tasks.at(0).at("result").at("message").get<std::string>() == "hello");
    BOOST_TEST(tasks.at(1).at("result").at("message").get<std::string>() == "hello again");

    // User-supplied metadata must survive into the saved task record
    BOOST_TEST(tasks.at(0).at("metadata").at("feature_id").get<std::string>() ==
               "greeting");

    // A feature_id supplies the task name; without one, use task_<task_id>
    BOOST_TEST(tasks.at(0).at("task_name").get<std::string>() == "greeting");
    BOOST_TEST(tasks.at(1).at("task_name").get<std::string>() == "task_1");
}

// Registered in examples/01_heat/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(heat, JobRecordChecker) {
    // The single statically linked BMI task must have a saved task record
    BOOST_REQUIRE_EQUAL(tasks.size(), 1);

    // With alpha = 1.5, dt = 1 / (4 * alpha) = 1/6; reaching time 20 takes 120 steps
    check_updates(120);
}

// Registered in examples/03_dynamic_bmi/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(dynamic_bmi, JobRecordChecker) {
    // Each of the C++ and Fortran plugins runs in-process and in a subprocess
    BOOST_REQUIRE_EQUAL(tasks.size(), 4);

    // The C++ tasks must use distinct registered units for the two execution modes
    BOOST_TEST(tasks.at(0).at("simulation_unit_id").get<std::size_t>() !=
               tasks.at(2).at("simulation_unit_id").get<std::size_t>());

    // The Fortran tasks must also use distinct units for the two execution modes
    BOOST_TEST(tasks.at(1).at("simulation_unit_id").get<std::size_t>() !=
               tasks.at(3).at("simulation_unit_id").get<std::size_t>());

    // Both models use alpha = 1, so dt = 0.25; all four runs need 40 steps to time 10
    check_updates(40);
}

// Registered in examples/07_job_controller/CMakeLists.txt using default example inputs
BOOST_FIXTURE_TEST_CASE(controller, JobRecordChecker) {
    // Heron iteration x_(n+1) = (x_n + 2 / x_n) / 2, starting at x_0 = 1
    const std::array estimates{1.5, 17.0 / 12.0, 577.0 / 408.0};

    // The controller must generate exactly three iterations, stopping at convergence
    BOOST_REQUIRE_EQUAL(tasks.size(), estimates.size());
    for (std::size_t id = 0; id < estimates.size(); ++id) {
        BOOST_TEST_CONTEXT("iteration " << id + 1) {
            const auto& output = tasks.at(id).at("result").at("output");
            const auto estimate = output.at("estimate").at("value").get<double>();
            const auto residual = output.at("residual").at("value").get<double>();

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

// These tests perform their own setup without JobRecordChecker. failure reads the case
// file and, when expected, the saved job record. controller_small_values reads only the
// saved job record; tests/run_example.cmake uses the case file to configure the example.

// Registered in the following examples:
//   examples/00_hello/CMakeLists.txt (case file: tests/cases/hello_*.json)
//   examples/03_dynamic_bmi/CMakeLists.txt (case file: tests/cases/dynamic_*.json)
//   examples/07_job_controller/CMakeLists.txt
//      (case file: tests/cases/controller_non_convergence.json)
BOOST_AUTO_TEST_CASE(failure) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    // Arguments: miaow_example_tests executable path, job-record path, and case-file path
    BOOST_REQUIRE_EQUAL(arguments.argc, 3);
    std::ifstream case_input{arguments.argv[2]};
    const auto expected = nlohmann::json::parse(case_input);
    const std::filesystem::path record_path{arguments.argv[1]};
    if (!expected.contains("tasks")) {
        // These cases fail before a final job record can be saved
        BOOST_TEST(!std::filesystem::exists(record_path));
        return;
    }

    std::ifstream job_record_input{record_path};
    const auto job_record = nlohmann::json::parse(job_record_input);
    const auto& tasks = job_record.at("tasks");
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
            const auto message = result.at("message").get<std::string>();
            BOOST_TEST(message.find(reference.at("message").get<std::string>()) !=
                       std::string::npos);
            const bool dispatched = reference.value("dispatched", true);

            // A worker must be assigned exactly when the task was dispatched
            BOOST_TEST(task.at("worker_id").is_null() == !dispatched);

            // Dispatch timing must be present for dispatched tasks and null otherwise
            BOOST_TEST(task.at("dispatch_offset_from_job_start").is_null() == !dispatched);
            if (dispatched) {
                // The assigned worker must belong to the case's configured worker pool
                BOOST_TEST(task.at("worker_id").get<std::size_t>() <
                           expected.value("max_workers", 2u));
            } else {
                // Tasks that never reached a worker must have zero run time
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
BOOST_AUTO_TEST_CASE(controller_small_values) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    // Arguments: miaow_example_tests executable path, job-record path, and case-file path
    BOOST_REQUIRE_EQUAL(arguments.argc, 3);
    std::ifstream job_record_input{arguments.argv[1]};
    const auto job_record = nlohmann::json::parse(job_record_input);
    const auto& tasks = job_record.at("tasks");

    // With radicand S = 2e-16 and x_0 = 1e-8, the expected sqrt(2) iterates are
    // scaled by 1e-8
    const std::array estimates{1.5e-8, (17.0 / 12.0) * 1e-8, (577.0 / 408.0) * 1e-8};

    // The smaller radicand and tolerance must still produce exactly three iterations
    BOOST_REQUIRE_EQUAL(tasks.size(), estimates.size());

    // Check each task's result against the expected scaled Heron's method iterates
    for (std::size_t id = 0; id < tasks.size(); ++id) {
        BOOST_TEST_CONTEXT("iteration " << id + 1) {
            const auto& result = tasks.at(id).at("result");

            // Every iteration must complete successfully even at this smaller scale
            BOOST_TEST(result.at("status").get<std::string>() == "succeeded");
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
