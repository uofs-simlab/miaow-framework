#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <caf/config_value.hpp>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

// Open stream-backed CSV outputs for each BMI output variable
struct OutputVariable {
    std::string name_;              // BMI output variable name
    std::size_t n_items_;           // Number of scalar items in the variable value
    std::ofstream stream_;          // CSV output stream for this variable
};

// Construct an initialized BMI-compliant forcing source from the structured
// forcing dictionary in `Task.args_[1]`
std::unique_ptr<::bmi::Bmi> make_forcing_bmi(
    const caf::config_value::dictionary& forcing_config,
    ::bmi::Bmi& model);

// Open CSV output files for all BMI output variables in output_dir
std::vector<OutputVariable> open_output_vars(
    ::bmi::Bmi& model,
    const std::string& output_dir);

// Write one timestep row to a variable CSV stream
void write_output_step(::bmi::Bmi& model, OutputVariable& variable, double time);

// Run the BMI subprocess executable
int run_subprocess_cli(int argc, char* argv[]);

} // namespace miaow::bmi
