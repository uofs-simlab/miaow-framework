#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

struct ForcingConfig {
    std::string source_type_;   // Forcing backend identifier, currently "csv"
    std::string source_path_;   // Absolute forcing CSV path after resolution
    std::string csv_column_;    // CSV column name used as the forcing series
    std::string bmi_input_var_; // BMI input variable to set each update step
    double time_step_size_{0.0}; // Forcing timestep size in declared time_units_
    std::string time_units_;     // Time units used for forcing timestep metadata
};

struct ForcingData {
    std::string bmi_input_var_; // BMI input variable receiving forcing values
    std::vector<double> values_; // Row-indexed forcing values, one per update
};

// Parse required key=value entries and normalize source_path_ as needed
ForcingConfig read_forcing_config(const std::string& forcing_config_path);

// Load forcing column values and validate compatibility with the BMI input
ForcingData load_forcing_data(::bmi::Bmi& model, const ForcingConfig& forcing_config);

// Run BMI updates with preloaded forcing and row-index forcing injection.
// update_steps is written as updates complete, so the count survives a throw.
void run_model_updates(
    ::bmi::Bmi& model,
    const std::optional<ForcingData>& forcing_data,
    const std::string& task_output_dir,
    std::size_t& update_steps);

// Parse optional forcing path and run the model update workflow.
// update_steps is written as updates complete, so the count survives a throw.
void run_model(
    ::bmi::Bmi& model,
    const std::optional<std::string>& forcing_config_path,
    const std::string& task_output_dir,
    std::size_t& update_steps);

} // namespace miaow::bmi
