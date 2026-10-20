#include "forcing_comparison.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

namespace {

const std::filesystem::path realization_path =
    "data/gauge_01073000/realization_config.json";

miaow::JobRecord run_forcing_case(const miaow::RuntimeOptions& options,
                                  bool use_netcdf) {
    miaow::SimulationUnitRegistry registry;
    const miaow::SimulationUnitId subprocess =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path(),
                                miaow::SubprocessLifetime::persistent);
    const miaow::PartitionedBmiRealization realization(realization_path.string());
    std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(subprocess, realization);

    if (use_netcdf) {
        // Read the model settings used by the packaged realization
        std::ifstream input(realization_path);
        const auto document = nlohmann::json::parse(input);
        const auto& params = document.at("global").at("formulations").at(0).at("params");
        const auto& module = params.at("modules").at(0).at("params");
        const std::filesystem::path base = std::filesystem::absolute(
            realization_path.parent_path());
        const std::string init_template = module.at("init_config").get<std::string>();
        miaow::DynamicBmiConfig config{
            .backend_ = miaow::DynamicBmiBackend::fortran,
            .plugin_path_ =
                (base / module.at("library_file").get<std::string>()).string(),
            .forcing_ = miaow::BmiForcingConfig{
                .format_ = miaow::BmiDataFormat::netcdf,
                .file_ = (base / "forcings/forcing.nc").string(),
                .feature_id_variable_ = "hruId",
                .variables_names_map_ = module.at("variables_names_map")
                                           .get<std::map<std::string, std::string>>(),
            },
            .output_ = miaow::BmiOutputConfig{
                .variables_ = params.at("output_variables").get<std::vector<std::string>>(),
                .interval_ = document.at("time").at("output_interval").get<double>(),
            },
        };

        for (miaow::Task& task : tasks) {
            const std::string label = task.metadata_.at("feature_id");
            std::string init_config = init_template;
            init_config.replace(init_config.find("{{id}}"), 6, label);
            config.init_config_ = (base / init_config).string();

            // File selection uses a numeric ID; reporting retains the catchment label
            const std::string feature_id = std::to_string(std::stoll(label.substr(4)));
            task = miaow::make_bmi_task(subprocess, config, feature_id,
                                        std::move(task.metadata_));
        }
    }

    // Each case owns its runtime so its children exit before the next case starts
    std::cout << "\nRunning " << (use_netcdf ? "NetCDF" : "CSV") << " forcing with "
              << options.max_workers_ << " worker(s)\n";
    miaow::Runtime runtime(registry, options);
    miaow::JobRecord record = runtime.submit_job(tasks).get();
    miaow::print_job_summary(record);
    if (!miaow::all_tasks_succeeded(record)) {
        throw std::runtime_error("Forcing comparison requires every SUMMA task to succeed");
    }
    if (record.tasks_.size() != 5) {
        throw std::runtime_error("Forcing comparison requires all five SUMMA catchments");
    }
    return record;
}

std::map<std::string, std::filesystem::path> output_paths(
    const miaow::JobRecord& record, const std::string& output_root) {
    const std::filesystem::path results =
        std::filesystem::path(output_root) / ("run_" + std::to_string(record.run_id_)) /
        ("job_" + std::to_string(record.job_id_)) / "results";
    std::map<std::string, std::filesystem::path> paths;
    for (const miaow::TaskRecord& task : record.tasks_) {
        paths.emplace(task.metadata_.at("feature_id"),
                      results / (task.task_name_ + ".csv"));
    }
    return paths;
}

// Runtime CSV output has unquoted headers and scalar numeric cells
std::vector<std::string> split_row(const std::string& row) {
    std::istringstream input(row);
    std::vector<std::string> cells;
    std::string cell;
    while (std::getline(input, cell, ',')) {
        cells.push_back(cell);
    }
    if (row.ends_with(',')) {
        cells.emplace_back();
    }
    return cells;
}

void compare_output(const std::string& feature, const std::filesystem::path& csv_path,
                     const std::filesystem::path& netcdf_path) {
    std::ifstream csv(csv_path);
    std::ifstream netcdf(netcdf_path);
    if (!csv.is_open() || !netcdf.is_open()) {
        throw std::runtime_error("Missing forcing-comparison output for " + feature +
                                 ": " + csv_path.string() + " or " + netcdf_path.string());
    }

    std::string csv_line;
    std::string netcdf_line;
    if (!std::getline(csv, csv_line) || !std::getline(netcdf, netcdf_line) ||
        csv_line != netcdf_line) {
        throw std::runtime_error("Output headers differ for " + feature);
    }
    const std::vector<std::string> headers = split_row(csv_line);
    if (headers.size() < 2 || headers.front() != "time") {
        throw std::runtime_error("Invalid output header for " + feature);
    }

    std::size_t row_count = 0;
    while (true) {
        const bool has_csv_row = static_cast<bool>(std::getline(csv, csv_line));
        const bool has_netcdf_row = static_cast<bool>(std::getline(netcdf, netcdf_line));
        if (has_csv_row != has_netcdf_row) {
            throw std::runtime_error("Output row counts differ for " + feature +
                                     " after " + std::to_string(row_count) + " rows");
        }
        if (!has_csv_row) {
            break;
        }

        const std::vector<std::string> csv_cells = split_row(csv_line);
        const std::vector<std::string> netcdf_cells = split_row(netcdf_line);
        if (csv_cells.size() != headers.size() || netcdf_cells.size() != headers.size()) {
            throw std::runtime_error("Incomplete output row for " + feature + " at row " +
                                     std::to_string(row_count + 1));
        }

        for (std::size_t column = 0; column < headers.size(); ++column) {
            const std::string context = feature + ", time " + csv_cells.front() +
                                        ", variable " + headers.at(column);
            const auto parse_finite = [&](const std::string& cell) {
                std::size_t consumed = 0;
                double value = 0.0;
                try {
                    value = std::stod(cell, &consumed);
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid output value for " + context +
                                             ": " + cell);
                }
                if (consumed != cell.size() || !std::isfinite(value)) {
                    throw std::runtime_error("Invalid output value for " + context +
                                             ": " + cell);
                }
                return value;
            };
            const double csv_value = parse_finite(csv_cells.at(column));
            const double netcdf_value = parse_finite(netcdf_cells.at(column));
            if (csv_value != netcdf_value) {
                std::ostringstream message;
                message << std::setprecision(std::numeric_limits<double>::max_digits10)
                        << "Forcing comparison mismatch for " << context
                        << ": CSV=" << csv_value << ", NetCDF=" << netcdf_value;
                throw std::runtime_error(message.str());
            }
        }
        ++row_count;
    }
    if (row_count == 0) {
        throw std::runtime_error("Empty output series for " + feature);
    }
    std::cout << feature << ": " << row_count << " output rows agree exactly\n";
}

} // namespace

int compare_forcing(const miaow::RuntimeOptions& runtime_options) {
    try {
        if (runtime_options.output_root_.empty()) {
            throw std::runtime_error("--compare_forcing requires a non-empty output root");
        }
        miaow::RuntimeOptions csv_options = runtime_options;
        miaow::RuntimeOptions netcdf_options = runtime_options;
        csv_options.output_root_ =
            (std::filesystem::path(runtime_options.output_root_) / "csv").string();
        netcdf_options.output_root_ =
            (std::filesystem::path(runtime_options.output_root_) / "netcdf").string();

        const miaow::JobRecord csv_record = run_forcing_case(csv_options, false);
        const miaow::JobRecord netcdf_record = run_forcing_case(netcdf_options, true);
        const auto csv_paths = output_paths(csv_record, csv_options.output_root_);
        const auto netcdf_paths = output_paths(netcdf_record, netcdf_options.output_root_);
        for (const auto& [feature, csv_path] : csv_paths) {
            const auto netcdf_path = netcdf_paths.find(feature);
            if (netcdf_path == netcdf_paths.end()) {
                throw std::runtime_error("Missing NetCDF output catchment: " + feature);
            }
            compare_output(feature, csv_path, netcdf_path->second);
        }
        std::cout << "SUMMA CSV and NetCDF forcing outputs agree exactly\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
}
