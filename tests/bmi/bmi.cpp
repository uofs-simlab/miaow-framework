#define BOOST_TEST_MODULE miaow_bmi
#include <boost/test/unit_test.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <netcdf.h>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "bmi/internal.hpp"

namespace {

struct NetCdfForcingFixture {
    std::vector<double> feature_ids_;
    std::vector<double> times_;
    std::vector<double> air_temperatures_;
    bool feature_indexed_temperature_{true};
    std::optional<double> temperature_fill_value_{-9999.0};
    bool write_temperatures_{true};
    nc_type temperature_type_{NC_DOUBLE};
    std::string packed_variable_;
    std::string packing_attribute_;
    std::vector<long long> signed_feature_ids_;
    std::vector<unsigned long long> unsigned_feature_ids_;
    std::optional<long long> feature_missing_value_;
    bool disable_temperature_fill_{false};
    std::optional<double> floating_feature_missing_value_;
};

void check_test_net_cdf(int status, const std::string& operation) {
    if (status != NC_NOERR) {
        throw std::runtime_error(operation + ": " + nc_strerror(status));
    }
}

void write_net_cdf_forcing_fixture(const std::filesystem::path& path,
                                   const NetCdfForcingFixture& fixture) {
    std::size_t feature_count = fixture.feature_ids_.size();
    nc_type feature_type = NC_DOUBLE;
    if (!fixture.signed_feature_ids_.empty()) {
        feature_count = fixture.signed_feature_ids_.size();
        feature_type = NC_INT64;
    } else if (!fixture.unsigned_feature_ids_.empty()) {
        feature_count = fixture.unsigned_feature_ids_.size();
        feature_type = NC_UINT64;
    }
    const std::size_t expected_temperature_count =
        fixture.feature_indexed_temperature_
            ? feature_count * fixture.times_.size()
            : fixture.times_.size();
    if (feature_count == 0 || fixture.times_.empty() ||
        fixture.air_temperatures_.size() != expected_temperature_count) {
        throw std::runtime_error("invalid NetCDF forcing test fixture");
    }

    int file_id = -1;
    const bool net_cdf4 = !fixture.signed_feature_ids_.empty() ||
                         !fixture.unsigned_feature_ids_.empty() ||
                         fixture.temperature_type_ >= NC_UBYTE ||
                         fixture.disable_temperature_fill_;
    check_test_net_cdf(nc_create(path.string().c_str(),
                                  NC_CLOBBER | (net_cdf4 ? NC_NETCDF4 : 0), &file_id),
                       "unable to create NetCDF test fixture");

    int time_dimension_id = -1;
    int feature_dimension_id = -1;
    check_test_net_cdf(nc_def_dim(file_id, "time", fixture.times_.size(),
                                  &time_dimension_id),
                       "unable to define test time dimension");
    check_test_net_cdf(nc_def_dim(file_id, "hru", feature_count,
                                  &feature_dimension_id),
                       "unable to define test feature dimension");

    int time_variable_id = -1;
    int feature_variable_id = -1;
    int temperature_variable_id = -1;
    check_test_net_cdf(nc_def_var(file_id, "time", NC_DOUBLE, 1, &time_dimension_id,
                                  &time_variable_id),
                       "unable to define test time variable");
    check_test_net_cdf(nc_def_var(file_id, "hruId", feature_type, 1, &feature_dimension_id,
                                  &feature_variable_id),
                       "unable to define test feature variable");
    if (fixture.feature_indexed_temperature_) {
        const std::array<int, 2> forcing_dimensions{time_dimension_id,
                                                    feature_dimension_id};
        check_test_net_cdf(nc_def_var(file_id, "air_temperature", fixture.temperature_type_,
                                      forcing_dimensions.size(), forcing_dimensions.data(),
                                      &temperature_variable_id),
                           "unable to define test forcing variable");
    } else {
        check_test_net_cdf(nc_def_var(file_id, "air_temperature", fixture.temperature_type_,
                                      1, &time_dimension_id, &temperature_variable_id),
                           "unable to define test forcing variable");
    }

    const std::string axis = "T";
    const std::string time_units = "seconds since 2000-01-01 00:00:00";
    const std::string temperature_units = "K";
    check_test_net_cdf(nc_put_att_text(file_id, time_variable_id, "axis", axis.size(),
                                       axis.data()),
                       "unable to write test time axis");
    check_test_net_cdf(nc_put_att_text(file_id, time_variable_id, "units",
                                       time_units.size(), time_units.data()),
                       "unable to write test time units");
    check_test_net_cdf(nc_put_att_text(file_id, temperature_variable_id, "units",
                                       temperature_units.size(), temperature_units.data()),
                       "unable to write test forcing units");
    // Disabling prefill clears _FillValue, so declare the missing-data marker afterward
    if (fixture.disable_temperature_fill_) {
        check_test_net_cdf(nc_def_var_fill(file_id, temperature_variable_id, 1, nullptr),
                           "unable to disable test forcing fill");
    }
    if (fixture.temperature_fill_value_) {
        check_test_net_cdf(nc_put_att_double(file_id, temperature_variable_id, "_FillValue",
                                             fixture.temperature_type_, 1,
                                             &*fixture.temperature_fill_value_),
                           "unable to write test forcing fill value");
    }
    if (!fixture.packed_variable_.empty()) {
        int variable_id = -1;
        check_test_net_cdf(nc_inq_varid(file_id, fixture.packed_variable_.c_str(),
                                         &variable_id),
                           "unable to find packed test variable");
        const double packing_value = 0.01;
        check_test_net_cdf(nc_put_att_double(file_id, variable_id,
                                             fixture.packing_attribute_.c_str(), NC_DOUBLE,
                                             1, &packing_value),
                           "unable to write test packing attribute");
    }
    if (fixture.feature_missing_value_) {
        check_test_net_cdf(nc_put_att_longlong(
                              file_id, feature_variable_id, "missing_value", NC_INT64, 1,
                              &*fixture.feature_missing_value_),
                           "unable to write test feature missing value");
    }
    if (fixture.floating_feature_missing_value_) {
        check_test_net_cdf(nc_put_att_double(file_id, feature_variable_id, "missing_value",
                                             NC_DOUBLE, 1,
                                             &*fixture.floating_feature_missing_value_),
                           "unable to write floating test feature missing value");
    }

    check_test_net_cdf(nc_enddef(file_id), "unable to finish defining NetCDF test fixture");
    check_test_net_cdf(nc_put_var_double(file_id, time_variable_id, fixture.times_.data()),
                       "unable to write test times");
    if (!fixture.signed_feature_ids_.empty()) {
        check_test_net_cdf(nc_put_var_longlong(file_id, feature_variable_id,
                                               fixture.signed_feature_ids_.data()),
                           "unable to write signed test feature identifiers");
    } else if (!fixture.unsigned_feature_ids_.empty()) {
        check_test_net_cdf(nc_put_var_ulonglong(file_id, feature_variable_id,
                                                fixture.unsigned_feature_ids_.data()),
                           "unable to write unsigned test feature identifiers");
    } else {
        check_test_net_cdf(nc_put_var_double(file_id, feature_variable_id,
                                             fixture.feature_ids_.data()),
                           "unable to write test feature identifiers");
    }
    if (fixture.write_temperatures_) {
        check_test_net_cdf(nc_put_var_double(file_id, temperature_variable_id,
                                             fixture.air_temperatures_.data()),
                           "unable to write test forcing values");
    }
    check_test_net_cdf(nc_close(file_id), "unable to close NetCDF test fixture");

    // Verify no-fill fixtures retain the intended metadata after reopening
    if (fixture.disable_temperature_fill_) {
        check_test_net_cdf(nc_open(path.string().c_str(), NC_NOWRITE, &file_id),
                           "unable to reopen NetCDF test fixture");
        check_test_net_cdf(nc_inq_varid(file_id, "air_temperature",
                                         &temperature_variable_id),
                           "unable to find test forcing variable");
        int no_fill = 0;
        check_test_net_cdf(nc_inq_var_fill(file_id, temperature_variable_id, &no_fill,
                                            nullptr),
                           "unable to inspect test forcing fill mode");
        double fill_value = 0.0;
        const int fill_status = nc_get_att_double(file_id, temperature_variable_id,
                                                   "_FillValue", &fill_value);
        check_test_net_cdf(nc_close(file_id), "unable to close NetCDF test fixture");

        BOOST_REQUIRE(no_fill != 0);
        if (fixture.temperature_fill_value_) {
            BOOST_REQUIRE_EQUAL(fill_status, NC_NOERR);
            BOOST_TEST(fill_value == *fixture.temperature_fill_value_);
        } else {
            BOOST_TEST(fill_status == NC_ENOTATT);
        }
    }
}

// Execute one direct dynamic task against a test-owned NetCDF forcing file
miaow::TaskResult run_net_cdf_forcing_fixture(const std::string& case_name,
                                              const NetCdfForcingFixture& fixture,
                                              const std::string& feature_id) {
    const std::filesystem::path test_root =
        std::filesystem::path(MIAOW_BMI_TEST_OUTPUT_ROOT) / ("net_cdf_" + case_name);
    const std::filesystem::path forcing_path = test_root / "forcing.nc";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);
    write_net_cdf_forcing_fixture(forcing_path, fixture);

    miaow::TaskResult result;
    {
        miaow::SimulationUnitRegistry registry;
        const miaow::SimulationUnitId dynamic_bmi =
            registry.add<miaow::DynamicBmiSimulationUnit>();
        miaow::Runtime runtime(registry);
        const miaow::DynamicBmiConfig config{
            .backend_ = miaow::DynamicBmiBackend::fortran,
            .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
            .forcing_ = miaow::BmiForcingConfig{
                .format_ = miaow::BmiDataFormat::netcdf,
                .file_ = forcing_path.string(),
                .feature_id_variable_ = "hruId",
                .variables_names_map_ = {{"temperature", "air_temperature"}},
            },
        };
        const miaow::JobRecord job_record =
            runtime.submit_job(miaow::make_bmi_task(dynamic_bmi, config, feature_id)).get();
        result = job_record.tasks_.at(0).result_;
    }

    std::filesystem::remove_all(test_root);
    return result;
}

} // namespace

// Build a static BMI task with its serialized configuration
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
    // Confirm the seconds tolerance survives internal task serialization
    const miaow::BmiPipelineConfig pipeline_config(task);
    BOOST_TEST(pipeline_config.time_step_tolerance_seconds_ ==
               miaow::default_bmi_time_step_tolerance_seconds);
}

// JSON module indices must be valid before conversion to the internal integer index.
BOOST_AUTO_TEST_CASE(pipeline_json_preserves_numbers_and_checks_module_indices) {
    auto pipeline_json = nlohmann::json::parse(R"({
        "global": {
            "formulations": [{
                "name": "bmi_multi",
                "params": {
                    "modules": [{
                        "name": "supplied_model",
                        "params": {"init_config": "model.ini"}
                    }],
                    "output_module": 0.0,
                    "time_step_tolerance_seconds": 1e-100
                }
            }]
        }
    })");
    const auto& pipeline_object = pipeline_json.get_ref<const nlohmann::json::object_t&>();
    const miaow::BmiPipelineConfig pipeline_config(pipeline_object, "BMI test error: ");
    BOOST_TEST(pipeline_config.output_module_ == 0);
    BOOST_TEST(pipeline_config.time_step_tolerance_seconds_ == 1e-100);

    auto& params = pipeline_json.at("global").at("formulations").at(0).at("params");
    for (const auto& invalid_index : std::vector<nlohmann::json>{-1, 0.5, 1e100}) {
        params.at("output_module") = invalid_index;
        BOOST_CHECK_THROW(miaow::BmiPipelineConfig(pipeline_object, "BMI test error: "),
                          std::runtime_error);
    }
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

// Enforce fields that distinguish CSV forcing from feature-indexed NetCDF forcing
BOOST_AUTO_TEST_CASE(forcing_config_enforces_format_specific_fields) {
    miaow::DynamicBmiConfig dynamic_config{
        .plugin_path_ = "model.so",
        .forcing_ = miaow::BmiForcingConfig{
            .file_ = "forcing.csv",
            .feature_id_variable_ = "hruId",
        },
    };
    BOOST_CHECK_THROW(miaow::make_bmi_task(0, dynamic_config), std::runtime_error);

    dynamic_config.forcing_->format_ = miaow::BmiDataFormat::netcdf;
    dynamic_config.forcing_->feature_id_variable_.reset();
    BOOST_CHECK_THROW(miaow::make_bmi_task(0, dynamic_config, "202"),
                      std::runtime_error);

    dynamic_config.forcing_->feature_id_variable_ = "hruId";
    BOOST_CHECK_THROW(miaow::make_bmi_task(0, dynamic_config), std::runtime_error);

    const miaow::StaticBmiConfig static_config{
        .init_config_ = "model.ini",
        .forcing_ = dynamic_config.forcing_,
    };
    BOOST_CHECK_THROW(miaow::make_bmi_task(0, static_config), std::runtime_error);
}

// Run the Fortran BMI lifecycle and write its resolved configuration
BOOST_AUTO_TEST_CASE(fortran_plugin_runs_in_process_and_writes_config_artifact) {
    const std::filesystem::path output_root = MIAOW_BMI_TEST_OUTPUT_ROOT;
    std::filesystem::remove_all(output_root);

    miaow::SimulationUnitRegistry registry;
    // A nonzero registration ID verifies that task creation preserves the chosen unit.
    registry.add<miaow::DynamicBmiSimulationUnit>();
    const miaow::SimulationUnitId dynamic_bmi =
        registry.add<miaow::DynamicBmiSimulationUnit>();
    miaow::Runtime runtime(registry, miaow::RuntimeOptions{1, std::chrono::minutes(1),
                                                           std::chrono::minutes(5),
                                                           output_root.string()});

    // Load the test-owned Fortran plugin through the public dynamic BMI path
    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
    };
    const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);
    BOOST_TEST(task.simulation_unit_id_ == dynamic_bmi);
    BOOST_REQUIRE_EQUAL(task.args_.size(), 1);

    const miaow::JobRecord job_record = runtime.submit_job(task).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    const miaow::TaskResult& task_result = job_record.tasks_.front().result_;
    BOOST_REQUIRE(task_result.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(task_result.message_ == "BMI workflow complete (updates: 2)");
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

    std::ostringstream output_contents;
    output_contents << output.rdbuf();
    BOOST_TEST(output_contents.str() ==
               "time,temperature\n"
               "0,0\n"
               "1,10\n"
               "2,20\n");

    output.close();
    std::filesystem::remove_all(output_root);
}

// Select one feature from a shared NetCDF file and apply its forcing values
BOOST_AUTO_TEST_CASE(net_cdf_feature_forcing_reaches_model) {
    const std::filesystem::path test_root =
        std::filesystem::path(MIAOW_BMI_TEST_OUTPUT_ROOT) / "net_cdf_forcing";
    const std::filesystem::path forcing_path = test_root / "forcing.nc";
    const std::filesystem::path output_root = test_root / "output";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);

    write_net_cdf_forcing_fixture(forcing_path,
                                  NetCdfForcingFixture{
                                      .feature_ids_ = {101.0, 202.0},
                                      .times_ = {5.0, 6.0, 7.0},
                                      .air_temperatures_ = {10.0, 100.0, 20.0, 200.0, 30.0,
                                                            300.0},
                                  });

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
            .format_ = miaow::BmiDataFormat::netcdf,
            .file_ = forcing_path.string(),
            .feature_id_variable_ = "hruId",
            .variables_names_map_ = {{"temperature", "air_temperature"}},
        },
        .output_ = miaow::BmiOutputConfig{
            .variables_ = {"temperature"},
        },
    };
    const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config, "202");
    const miaow::BmiPipelineConfig pipeline_config(task);
    BOOST_CHECK(pipeline_config.forcing_format_ == miaow::BmiDataFormat::netcdf);
    BOOST_REQUIRE(pipeline_config.forcing_feature_id_variable_.has_value());
    BOOST_TEST(*pipeline_config.forcing_feature_id_variable_ == "hruId");
    BOOST_TEST(pipeline_config.feature_id_ == "202");

    const miaow::JobRecord job_record = runtime.submit_job(task).get();
    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_REQUIRE(job_record.tasks_.front().result_.status_ ==
                  miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");

    const std::filesystem::path job_directory =
        output_root / ("run_" + std::to_string(job_record.run_id_)) /
        ("job_" + std::to_string(job_record.job_id_));
    std::ifstream output(job_directory / "results" / "task_0.csv");
    BOOST_REQUIRE(output.is_open());
    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "time,temperature");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "0,0");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "1,100");
    BOOST_REQUIRE(static_cast<bool>(std::getline(output, line)));
    BOOST_TEST(line == "2,200");
    BOOST_TEST(!static_cast<bool>(std::getline(output, line)));

    std::ifstream artifact(job_directory / "task_0.json");
    BOOST_REQUIRE(artifact.is_open());
    std::ostringstream artifact_contents;
    artifact_contents << artifact.rdbuf();
    BOOST_TEST(artifact_contents.str().find("\"provider\":\"NetCdf\"") !=
               std::string::npos);
    BOOST_TEST(artifact_contents.str().find("\"feature_id_variable\":\"hruId\"") !=
               std::string::npos);
    BOOST_TEST(artifact_contents.str().find("\"feature\":{\"id\":\"202\"}") !=
               std::string::npos);

    std::filesystem::remove_all(test_root);
}

// Reject a task feature that is absent from the configured identifier variable
BOOST_AUTO_TEST_CASE(net_cdf_rejects_missing_feature) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "missing_feature",
        NetCdfForcingFixture{
            .feature_ids_ = {101.0, 202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 100.0, 20.0, 200.0, 30.0, 300.0},
        },
        "303");

    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(result.message_.find("does not contain feature '303'") !=
               std::string::npos);
}

// Reject a mapped forcing variable that does not use the feature dimension
BOOST_AUTO_TEST_CASE(net_cdf_rejects_invalid_forcing_dimensions) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "invalid_dimensions",
        NetCdfForcingFixture{
            .feature_ids_ = {101.0, 202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
            .feature_indexed_temperature_ = false,
        },
        "202");

    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(result.message_.find("contains no numeric variables with the time and "
                                    "feature dimensions") != std::string::npos);
}

// Reject NetCDF time spacing that differs from the receiving model timestep
BOOST_AUTO_TEST_CASE(net_cdf_rejects_timestep_mismatch) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "timestep_mismatch",
        NetCdfForcingFixture{
            .feature_ids_ = {101.0, 202.0},
            .times_ = {0.0, 2.0, 4.0},
            .air_temperatures_ = {10.0, 100.0, 20.0, 200.0, 30.0, 300.0},
        },
        "202");

    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(result.message_.starts_with("Forcing alignment error: forcing timestep"));
}

// Reject a selected NetCDF forcing value marked with the variable fill value
BOOST_AUTO_TEST_CASE(net_cdf_rejects_missing_forcing_value) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "missing_value",
        NetCdfForcingFixture{
            .feature_ids_ = {101.0, 202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, -9999.0, 20.0, 200.0, 30.0, 300.0},
        },
        "202");

    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(result.message_.find("contains a missing value for feature '202' at time "
                                    "index 0") != std::string::npos);
}

// Reject unwritten cells without requiring an explicit _FillValue attribute
BOOST_AUTO_TEST_CASE(net_cdf_rejects_default_fill_values) {
    for (const nc_type type : {NC_BYTE, NC_SHORT, NC_INT, NC_FLOAT, NC_DOUBLE,
                               NC_UBYTE, NC_USHORT, NC_UINT, NC_INT64, NC_UINT64}) {
        BOOST_TEST_CONTEXT("NetCDF variable type " << type) {
            const miaow::TaskResult result = run_net_cdf_forcing_fixture(
                "default_fill_" + std::to_string(type),
                NetCdfForcingFixture{
                    .feature_ids_ = {101.0, 202.0},
                    .times_ = {0.0, 1.0, 2.0},
                    .air_temperatures_ = {10.0, 100.0, 20.0, 200.0, 30.0, 300.0},
                    .temperature_fill_value_ = std::nullopt,
                    .write_temperatures_ = false,
                    .temperature_type_ = type,
                },
                "202");
            BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
            BOOST_TEST(result.message_.find("contains a missing value for feature '202' "
                                            "at time index 0") != std::string::npos);
        }
    }
}

// An explicit fill value replaces the type default as the missing-data marker
BOOST_AUTO_TEST_CASE(net_cdf_explicit_fill_overrides_default) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "explicit_fill_override",
        NetCdfForcingFixture{
            .feature_ids_ = {202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {NC_FILL_SHORT, 20.0, 30.0},
            .temperature_type_ = NC_SHORT,
        },
        "202");
    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::succeeded);
}

// Disabling fill must not introduce a zero-valued missing-data marker
BOOST_AUTO_TEST_CASE(net_cdf_no_fill_accepts_zero) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "no_fill_zero",
        NetCdfForcingFixture{
            .feature_ids_ = {202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {0.0, 20.0, 30.0},
            .temperature_fill_value_ = std::nullopt,
            .disable_temperature_fill_ = true,
        },
        "202");
    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::succeeded);
}

// An explicit missing-data marker must be honoured even when prefill is disabled
BOOST_AUTO_TEST_CASE(net_cdf_no_fill_honours_explicit_fill) {
    const miaow::TaskResult result = run_net_cdf_forcing_fixture(
        "no_fill_explicit_marker",
        NetCdfForcingFixture{
            .feature_ids_ = {202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {-9999.0, 20.0, 30.0},
            .disable_temperature_fill_ = true,
        },
        "202");
    BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(result.message_.find("contains a missing value for feature '202'") !=
               std::string::npos);
}

// Missing coordinates and identifiers must not be interpreted as valid numbers
BOOST_AUTO_TEST_CASE(net_cdf_rejects_default_fill_in_coordinates) {
    const miaow::TaskResult feature_result = run_net_cdf_forcing_fixture(
        "feature_default_fill",
        NetCdfForcingFixture{
            .feature_ids_ = {NC_FILL_DOUBLE, 202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 100.0, 20.0, 200.0, 30.0, 300.0},
        },
        "202");
    BOOST_REQUIRE(feature_result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(feature_result.message_.find("identifier variable 'hruId' contains a "
                                            "missing value") != std::string::npos);

    for (const std::size_t missing_index : {0u, 1u}) {
        NetCdfForcingFixture fixture{
            .feature_ids_ = {202.0},
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
        };
        fixture.times_.at(missing_index) = NC_FILL_DOUBLE;
        const miaow::TaskResult time_result = run_net_cdf_forcing_fixture(
            "time_default_fill_" + std::to_string(missing_index), fixture, "202");
        BOOST_REQUIRE(time_result.status_ == miaow::TaskStatus::failed);
        BOOST_TEST(time_result.message_.find("time coordinate contains a missing value") !=
                   std::string::npos);
    }
}

// Reject either packing attribute on any variable consumed by the reader
BOOST_AUTO_TEST_CASE(net_cdf_rejects_packed_variables) {
    for (const std::string variable : {"air_temperature", "time", "hruId"}) {
        for (const std::string attribute : {"scale_factor", "add_offset"}) {
            const miaow::TaskResult result = run_net_cdf_forcing_fixture(
                "packed_" + variable + "_" + attribute,
                NetCdfForcingFixture{
                    .feature_ids_ = {202.0},
                    .times_ = {0.0, 1.0, 2.0},
                    .air_temperatures_ = {10.0, 20.0, 30.0},
                    .packed_variable_ = variable,
                    .packing_attribute_ = attribute,
                },
                "202");
            BOOST_REQUIRE(result.status_ == miaow::TaskStatus::failed);
            BOOST_TEST(result.message_.find("packed NetCDF variable '" + variable +
                                            "' uses '" + attribute + "'") !=
                       std::string::npos);
        }
    }
}

// Adjacent 64-bit identifiers and markers must remain distinct above 2^53
BOOST_AUTO_TEST_CASE(net_cdf_integer_feature_ids_remain_exact) {
    const std::filesystem::path test_root =
        std::filesystem::path(MIAOW_BMI_TEST_OUTPUT_ROOT) / "net_cdf_integer_ids";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);
    const std::filesystem::path forcing_path = test_root / "forcing.nc";
    for (const bool unsigned_ids : {false, true}) {
        NetCdfForcingFixture fixture{
            .times_ = {0.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
            .feature_missing_value_ = 9007199254740994LL,
        };
        if (unsigned_ids) {
            fixture.unsigned_feature_ids_ = {9007199254740992ULL, 9007199254740993ULL,
                                             9223372036854775807ULL};
        } else {
            fixture.signed_feature_ids_ = {9007199254740992LL, 9007199254740993LL,
                                           9223372036854775807LL};
        }
        write_net_cdf_forcing_fixture(forcing_path, fixture);
        const std::array<std::string, 3> feature_ids{
            "9007199254740992", "9007199254740993", "9223372036854775807"};
        for (std::size_t index = 0; index < feature_ids.size(); ++index) {
            miaow::bmi::NetCdfForcingReader reader(forcing_path, "hruId",
                                                  feature_ids.at(index));
            BOOST_REQUIRE(reader.advance());
            double value = 0.0;
            reader.copy_value(0, &value);
            BOOST_TEST(value == fixture.air_temperatures_.at(index));
        }
    }
    std::filesystem::remove_all(test_root);
}

// Keep the signed lower bound and reject unsigned values outside the supported range
BOOST_AUTO_TEST_CASE(net_cdf_integer_feature_id_bounds) {
    const miaow::TaskResult signed_result = run_net_cdf_forcing_fixture(
        "signed_id_minimum",
        NetCdfForcingFixture{
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
            .signed_feature_ids_ = {std::numeric_limits<long long>::min()},
        },
        "-9223372036854775808");
    BOOST_REQUIRE(signed_result.status_ == miaow::TaskStatus::succeeded);

    const miaow::TaskResult unsigned_result = run_net_cdf_forcing_fixture(
        "unsigned_id_out_of_range",
        NetCdfForcingFixture{
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
            .unsigned_feature_ids_ = {9223372036854775808ULL},
        },
        "9223372036854775808");
    BOOST_REQUIRE(unsigned_result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(unsigned_result.message_.find("signed 64-bit integers") !=
               std::string::npos);
}

// Compare 64-bit missing markers exactly, including defaults outside the signed range
BOOST_AUTO_TEST_CASE(net_cdf_integer_feature_missing_values_remain_exact) {
    NetCdfForcingFixture fixture{
        .times_ = {0.0, 1.0, 2.0},
        .air_temperatures_ = {10.0, 20.0, 30.0},
        .signed_feature_ids_ = {9007199254740992LL},
        .feature_missing_value_ = 9007199254740993LL,
    };
    const miaow::TaskResult valid_result =
        run_net_cdf_forcing_fixture("integer_id_near_missing", fixture, "9007199254740992");
    BOOST_REQUIRE(valid_result.status_ == miaow::TaskStatus::succeeded);

    fixture.signed_feature_ids_ = {9007199254740993LL};
    const miaow::TaskResult missing_result =
        run_net_cdf_forcing_fixture("integer_id_missing", fixture, "9007199254740993");
    BOOST_REQUIRE(missing_result.status_ == miaow::TaskStatus::failed);
    BOOST_TEST(missing_result.message_.find(
                   "identifier variable 'hruId' contains a missing value") !=
               std::string::npos);

    fixture.feature_missing_value_.reset();
    for (const bool unsigned_ids : {false, true}) {
        fixture.signed_feature_ids_.clear();
        if (unsigned_ids) {
            fixture.unsigned_feature_ids_ = {NC_FILL_UINT64};
        } else {
            fixture.signed_feature_ids_ = {NC_FILL_INT64};
        }
        const miaow::TaskResult default_result = run_net_cdf_forcing_fixture(
            "integer_id_default_fill_" + std::to_string(unsigned_ids), fixture, "202");
        BOOST_REQUIRE(default_result.status_ == miaow::TaskStatus::failed);
        BOOST_TEST(default_result.message_.find("identifier variable 'hruId' contains a "
                                                "missing value") != std::string::npos);
    }
}

// A fractional missing marker must not be truncated to a valid integer identifier
BOOST_AUTO_TEST_CASE(net_cdf_integer_ids_do_not_truncate_floating_markers) {
    for (const bool unsigned_ids : {false, true}) {
        NetCdfForcingFixture fixture{
            .times_ = {0.0, 1.0, 2.0},
            .air_temperatures_ = {10.0, 20.0, 30.0},
            .floating_feature_missing_value_ = 0.5,
        };
        if (unsigned_ids) {
            fixture.unsigned_feature_ids_ = {0};
        } else {
            fixture.signed_feature_ids_ = {0};
        }
        const miaow::TaskResult result = run_net_cdf_forcing_fixture(
            "integer_id_fractional_marker_" + std::to_string(unsigned_ids), fixture, "0");
        BOOST_REQUIRE(result.status_ == miaow::TaskStatus::succeeded);
    }
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

    output.close();
    config_artifact.close();
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

    output.close();
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

    std::ostringstream output_contents;
    output_contents << output.rdbuf();
    BOOST_TEST(output_contents.str() ==
               "time,temperature\n"
               "0,0\n"
               "2,20\n");

    output.close();
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

    std::ostringstream output_contents;
    output_contents << output.rdbuf();
    BOOST_TEST(output_contents.str() ==
               "time,temperature\n"
               "0,0\n"
               "1,0\n"
               "2,0\n");

    output.close();
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
BOOST_AUTO_TEST_CASE(fortran_plugin_runs_in_fresh_subprocess) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId bmi_subprocess =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path(),
                                miaow::SubprocessLifetime::single_task);
    miaow::Runtime runtime(registry);

    const miaow::DynamicBmiConfig config{
        .backend_ = miaow::DynamicBmiBackend::fortran,
        .plugin_path_ = MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH,
    };
    const miaow::Task task = miaow::make_bmi_task(bmi_subprocess, config);
    const miaow::JobRecord job_record = runtime.submit_job(task).get();

    BOOST_REQUIRE_EQUAL(job_record.tasks_.size(), 1);
    BOOST_CHECK(job_record.tasks_.front().result_.status_ == miaow::TaskStatus::succeeded);
    BOOST_TEST(job_record.tasks_.front().result_.message_ ==
               "BMI workflow complete (updates: 2)");
    BOOST_REQUIRE(job_record.tasks_.front().worker_id_.has_value());
    BOOST_TEST(*job_record.tasks_.front().worker_id_ == 0);
}
