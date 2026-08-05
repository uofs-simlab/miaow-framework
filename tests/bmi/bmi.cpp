#define BOOST_TEST_MODULE miaow_bmi
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "bmi/internal.hpp"

// Build a static BMI task with its serialized config and in-process default
BOOST_AUTO_TEST_CASE(static_config_creates_a_task) {
    const miaow::StaticBmiConfig config{
        .init_config_ = "model.ini",
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = "forcing.csv",
            .variables_names_map_ = {{"temperature", "air_temperature"}},
        },
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
        },
    };

    const miaow::Task task = miaow::make_bmi_task(7, config);

    BOOST_TEST(task.simulation_unit_id_ == 7);
    BOOST_REQUIRE_EQUAL(task.args_.size(), 1);
    BOOST_CHECK(task.execution_mode_ == miaow::ExecutionMode::in_process);

    // Confirm the seconds tolerance survives internal task serialization
    const miaow::BmiPipelineConfig pipeline_config(task);
    BOOST_TEST(pipeline_config.time_step_tolerance_seconds_ ==
               miaow::default_bmi_time_step_tolerance_seconds);
}

// Convert supported fixed-duration BMI time units to seconds
BOOST_AUTO_TEST_CASE(bmi_time_units_convert_to_seconds) {
    constexpr std::string_view error_prefix = "BMI test error: ";
    BOOST_TEST(miaow::bmi::seconds_per_time_unit("s", error_prefix) == 1.0);
    BOOST_TEST(miaow::bmi::seconds_per_time_unit("minutes", error_prefix) == 60.0);
    BOOST_TEST(miaow::bmi::seconds_per_time_unit("HOURS", error_prefix) == 3600.0);
    BOOST_TEST(miaow::bmi::seconds_per_time_unit("d", error_prefix) == 86400.0);
}

// Apply the configured seconds tolerance to forcing timestep alignment
BOOST_AUTO_TEST_CASE(forcing_alignment_uses_seconds_tolerance) {
    BOOST_CHECK_NO_THROW(miaow::bmi::validate_forcing_time_step_delta(
        3600.00005, 3600.0, 1e-4, 2));
    BOOST_CHECK_THROW(miaow::bmi::validate_forcing_time_step_delta(
                          3600.0002, 3600.0, 1e-4, 2),
                      std::runtime_error);
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

// Write the resolved pipeline configuration beside task results
BOOST_AUTO_TEST_CASE(pipeline_writes_resolved_config_artifact) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    // Run one task so lifecycle execution writes the resolved config artifact
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    const miaow::TaskResult& task_result = job_record.tasks_.front().result_;
    BOOST_REQUIRE(task_result.status_ == miaow::TaskStatus::succeeded);
    BOOST_REQUIRE_EQUAL(task_result.artifact_paths_.size(), 1);
    BOOST_TEST(task_result.artifact_paths_.front() == "task_0.json");

    const std::filesystem::path config_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "task_0.json";
    BOOST_REQUIRE(std::filesystem::exists(config_path));
    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "task_0.csv";
    BOOST_TEST(!std::filesystem::exists(output_path));

    std::filesystem::remove_all(output_root);
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
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = MIAOW_TEST_BMI_FORCING_PATH,
            .variables_names_map_ = {{"temperature", "air_temperature"}},
        },
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
        },
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
    BOOST_TEST(line == "0,0");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "1,10");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "2,20");
    BOOST_TEST(!static_cast<bool>(std::getline(output, line)));

    std::filesystem::remove_all(output_root);
}

// Resolve an enabled empty selection to every BMI output variable
BOOST_AUTO_TEST_CASE(empty_output_variables_write_all_task_outputs) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .output_ = miaow::BmiOutputConfig{},
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);

    // Verify the task CSV contains the fixture's complete BMI output surface
    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "task_0.csv";
    std::ifstream output(output_path);
    BOOST_REQUIRE(output.is_open());

    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");

    // Verify the resolved artifact records selection of every output variable
    const std::filesystem::path config_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "task_0.json";
    std::ifstream config_artifact(config_path);
    BOOST_REQUIRE(config_artifact.is_open());
    std::ostringstream config_contents;
    config_contents << config_artifact.rdbuf();
    BOOST_TEST(config_contents.str().find("\"output_variables\":[]") !=
               std::string::npos);

    std::filesystem::remove_all(output_root);
}

// Reject variable aliases that do not identify BMI model inputs
BOOST_AUTO_TEST_CASE(forcing_alias_keys_must_be_model_inputs) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry);

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = MIAOW_TEST_BMI_FORCING_PATH,
            .variables_names_map_ = {{"temperature_alias", "air_temperature"}},
        },
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::failed);
    BOOST_TEST(job_record.tasks_.front().result_.message_.find(
                   "variables_names_map key 'temperature_alias' is not a BMI input "
                   "variable") != std::string::npos);
}

// Apply the NextGen output default when a realization omits output_variables
BOOST_AUTO_TEST_CASE(realization_without_output_variables_writes_all_outputs) {
    const std::filesystem::path test_root =
        std::filesystem::path(MIAOW_BMI_TEST_OUTPUT_ROOT) / "realization_default";
    const std::filesystem::path config_directory = test_root / "configs";
    const std::filesystem::path forcing_directory = test_root / "forcings";
    const std::filesystem::path output_root = test_root / "output";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(config_directory);
    std::filesystem::create_directories(forcing_directory);

    // Create the feature-specific model and forcing files required by the realization
    std::ofstream(config_directory / "cat-1.ini").close();
    std::ofstream forcing(forcing_directory / "cat-1.csv");
    BOOST_REQUIRE(forcing.is_open());
    forcing << "time,temperature\n"
            << "2000-01-01 00:00:00,10\n"
            << "2000-01-01 00:00:01,20\n";
    forcing.close();

    // Create the sibling feature collection used to partition the realization
    std::ofstream catchments(test_root / "catchments.geojson");
    BOOST_REQUIRE(catchments.is_open());
    catchments << R"({"type":"FeatureCollection","features":[)"
               << R"({"type":"Feature","id":"cat-1","properties":{},)"
               << R"("geometry":null}]})";
    catchments.close();

    // Omit output_variables so the adapter requests every model output
    const std::filesystem::path realization_path = test_root / "realization.json";
    std::ofstream realization(realization_path);
    BOOST_REQUIRE(realization.is_open());
    realization << R"({"global":{"formulations":[{"name":"bmi_multi","params":{)"
                << R"("model_type_name":"bmi_multi_test",)"
                << R"("main_output_variable":"temperature",)"
                << R"("modules":[{"name":"bmi_fortran","params":{)"
                << R"("model_type_name":"bmi_fortran_test",)"
                << R"("library_file":")" << MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH
                << R"(","init_config":"configs/{{id}}.ini",)"
                << R"("main_output_variable":"temperature",)"
                << R"("uses_forcing_file":false}}]}}],)"
                << R"("forcing":{"path":"forcings","provider":"CsvPerFeature",)"
                << R"("file_pattern":".*{{id}}.*"}},)"
                << R"("time":{"start_time":"2000-01-01 00:00:00",)"
                << R"("end_time":"2000-01-01 00:00:02"}})";
    realization.close();

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});
    const miaow::PartitionedBmiRealization partitioned(realization_path.string());
    const std::vector<miaow::Task> tasks =
        miaow::make_bmi_tasks(dynamic_bmi, partitioned);
    const miaow::JobRecord job_record = runtime.submit_job(tasks).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);
    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "cat-1.csv";
    std::ifstream output(output_path);
    BOOST_REQUIRE(output.is_open());
    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");

    std::filesystem::remove_all(test_root);
}

// Reject zero because a configured output interval must advance model time
BOOST_AUTO_TEST_CASE(zero_output_interval_is_rejected) {
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
            .interval_ = 0.0,
        },
    };

    BOOST_CHECK_EXCEPTION(
        miaow::make_bmi_task(0, config), std::runtime_error,
        [](const std::runtime_error& exception) {
            return std::string(exception.what()) ==
                   "BMI dynamic config error: output interval must be finite and positive";
        });
}

// Write the final state when the run ends before the next output interval
BOOST_AUTO_TEST_CASE(output_interval_writes_final_state) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = MIAOW_TEST_BMI_FORCING_PATH,
            .variables_names_map_ = {{"temperature", "air_temperature"}},
        },
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
            .interval_ = 3.0,
        },
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);

    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "task_0.csv";
    std::ifstream output(output_path);
    BOOST_REQUIRE(output.is_open());

    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "0,0");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "2,20");
    BOOST_TEST(!static_cast<bool>(std::getline(output, line)));

    std::filesystem::remove_all(output_root);
}

// Accept output boundaries within the configured seconds tolerance
BOOST_AUTO_TEST_CASE(output_interval_uses_configured_tolerance) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
        .time_step_tolerance_seconds_ = 1e-6,
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
            .interval_ = 1.0 + 4e-7,
        },
    };
    const miaow::JobRecord job_record =
        runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config)).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");

    const std::filesystem::path output_path =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_)) / "results" / "task_0.csv";
    std::ifstream output(output_path);
    BOOST_REQUIRE(output.is_open());

    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "0,0");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "1,0");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "2,0");
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
