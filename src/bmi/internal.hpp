#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "third_party/bmi/bmi.hxx"

namespace miaow::bmi {

// Open stream-backed CSV outputs for each BMI output variable
struct OutputVariable {
    std::string name_;              // BMI output variable name
    std::size_t n_items_;           // Number of scalar items in the variable value
    std::ofstream stream_;          // CSV output stream for this variable
};

// Construct a BMI-compliant forcing source. Caller must invoke Initialize with a
// forcing config path before any other BMI call.
std::unique_ptr<::bmi::Bmi> make_forcing_bmi();

// Validate that an initialized forcing model can drive model: its single output
// variable must name a scalar double input on model, and time step/units must match.
void validate_forcing_coupling(::bmi::Bmi& forcing, ::bmi::Bmi& model);

// Open CSV output files for all BMI output variables in output_dir
std::vector<OutputVariable> open_output_vars(
    ::bmi::Bmi& model,
    const std::string& output_dir);

// Write one timestep row to a variable CSV stream
void write_output_step(::bmi::Bmi& model, OutputVariable& variable, double time);

} // namespace miaow::bmi
