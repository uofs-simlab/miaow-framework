#define BOOST_TEST_MODULE miaow_bmi
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "miaow.hpp"
#include "miaow_bmi.hpp"

// Build a static BMI task with its serialized config and in-process default
BOOST_AUTO_TEST_CASE(static_config_creates_a_task) {
    const miaow::StaticBmiConfig config{
        .init_config_ = "model.ini",
        .forcing_file_ = "forcing.csv",
        .variables_names_map_ = {{"temperature", "air_temperature"}},
        .output_variables_ = {"temperature"},
    };

    const miaow::Task task = miaow::make_bmi_task(7, config);

    BOOST_TEST(task.simulation_unit_id_ == 7);
    BOOST_REQUIRE_EQUAL(task.args_.size(), 1);
    BOOST_CHECK(task.execution_mode_ == miaow::ExecutionMode::in_process);
}

// Preserve dynamic plugin routing fields when constructing a BMI task
BOOST_AUTO_TEST_CASE(dynamic_config_preserves_execution_fields) {
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = "model.so",
        .execution_mode_ = miaow::ExecutionMode::persistent_subprocess,
        .subprocess_path_ = "miaow_subprocess",
    };

    const miaow::Task task = miaow::make_bmi_task(9, config);

    BOOST_TEST(task.simulation_unit_id_ == 9);
    BOOST_REQUIRE_EQUAL(task.args_.size(), 1);
    BOOST_CHECK(task.execution_mode_ == miaow::ExecutionMode::persistent_subprocess);
    BOOST_TEST(task.subprocess_path_ == "miaow_subprocess");
}

// Exercise dynamic Fortran BMI loading and lifecycle execution in the parent process
BOOST_AUTO_TEST_CASE(fortran_plugin_runs_in_process) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry);

    // Load the test-owned Fortran plugin through the public dynamic BMI path
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
    };
    const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);

    const miaow::JobRecord job_record = runtime.submit_job(task).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");
}

// Apply two CSV forcing values to the model and expose them through task output
BOOST_AUTO_TEST_CASE(forcing_values_reach_model) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    // Couple the differently named CSV column to the fixture's scalar input
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .forcing_file_ = MIAOW_TEST_BMI_FORCING_PATH,
        .variables_names_map_ = {{"temperature", "air_temperature"}},
        .output_variables_ = {"temperature"},
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");

    // Verify each forcing value appears after its corresponding model update
    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "task_0.csv";
    std::ifstream output(output_path);
    BOOST_REQUIRE(output.is_open());

    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "1,10");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "2,20");
    BOOST_TEST(!static_cast<bool>(std::getline(output, line)));

    std::filesystem::remove_all(output_root);
}

// Convert dynamic library loading errors into failed task results
BOOST_AUTO_TEST_CASE(missing_dynamic_plugin_marks_task_failed) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry);

    const std::filesystem::path missing_plugin_path =
        std::filesystem::path(MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH).parent_path() /
        "missing_dynamic_bmi_plugin";
    BOOST_REQUIRE(!std::filesystem::exists(missing_plugin_path));

    const miaow::DynamicBmiConfig config{
        .plugin_path_ = missing_plugin_path.string(),
    };
    const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);
    const miaow::JobRecord job_record = runtime.submit_job(task).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(job_record.tasks_.front().result_.message_.find(
                   "BMI dynamic C++ plugin error: unable to load plugin") !=
               std::string::npos);
}

// Exercise the Fortran BMI lifecycle through a fresh task-owned subprocess
BOOST_AUTO_TEST_CASE(fortran_plugin_runs_in_single_task_subprocess) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry);

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .execution_mode_ = miaow::ExecutionMode::single_task_subprocess,
    };
    const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);
    const miaow::JobRecord job_record = runtime.submit_job(task).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");
}
