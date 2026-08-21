#include "bmi/internal.hpp"
#include "io/internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace miaow::bmi {

BmiPipeline::BmiPipeline(std::unique_ptr<::bmi::Bmi> supplied_model) {
    if (supplied_model) {
        modules_.push_back(std::move(supplied_model));
    }
}

// Add a model module to the pipeline and return its module index
std::size_t BmiPipeline::add_model_module(const BmiModuleConfig& module_config) {
    if (!module_config.backend_) {
        // Supplied model modules are bound to the model passed by the caller
        if (modules_.empty()) {
            io::throw_config_error("BMI pipeline config error: ",
                                   "module 'supplied_model' requires a supplied BMI model");
        }
    } else {
        // Dynamic configuration replaces any unused supplied model
        modules_.clear();
        modules_.push_back(std::make_unique<DynamicBmiModel>(module_config));
    }

    return modules_.size() - 1;
}

// Add the optional forcing module and return whether it is present
bool BmiPipeline::add_forcing_module(BmiDataFormat forcing_format,
                                     const std::string& forcing_file,
                                     const std::optional<std::string>& feature_id_variable,
                                     const std::string& feature_id,
                                     double time_step_tolerance_seconds,
                                     std::size_t model_module_index,
                                     std::size_t& forcing_module_index) {
    if (forcing_file.empty()) {
        return false;
    }

    // The forcing module follows every configured model module
    forcing_module_index = modules_.size();

    // The receiving model clock defines forcing timestep alignment
    modules_.push_back(std::make_unique<ForcingBmi>(*modules_.at(model_module_index),
                                                    forcing_format, feature_id_variable,
                                                    feature_id,
                                                    time_step_tolerance_seconds));
    return true;
}

// Build validated couplings from source outputs to target inputs
void BmiPipeline::build_couplings(
    std::size_t source_module_index, std::size_t target_module_index,
    const std::map<std::string, std::string>& variables_names_map) {
    ::bmi::Bmi& source_model = *modules_.at(source_module_index);
    ::bmi::Bmi& target_model = *modules_.at(target_module_index);

    const std::vector<std::string> source_outputs = source_model.GetOutputVarNames();
    const std::vector<std::string> target_inputs = target_model.GetInputVarNames();

    const auto append_validated_coupling = [&](const std::string& source_variable,
                                               const std::string& target_variable) {
        // Variable maps apply only to model inputs receiving external forcing
        const std::vector<std::string>::const_iterator target_it =
            std::find(target_inputs.begin(), target_inputs.end(), target_variable);
        if (target_it == target_inputs.end()) {
            throw std::runtime_error(
                "BMI forcing error: variables_names_map key '" + target_variable +
                "' is not a BMI input variable; MIAOW maps BMI input variables to "
                "forcing source variables");
        }

        // Source variables must be available as BMI outputs
        const std::vector<std::string>::const_iterator source_it =
            std::find(source_outputs.begin(), source_outputs.end(), source_variable);
        if (source_it == source_outputs.end()) {
            throw std::runtime_error("BMI coupling error: source variable '" +
                                     source_variable + "' not found");
        }

        // Construct and validate complete endpoint metadata once
        couplings_.emplace_back(source_module_index, source_model, source_variable,
                                target_module_index, target_model, target_variable);
    };

    if (variables_names_map.empty()) {
        // Empty maps use same-named forcing headers for every model input
        for (const std::string& raw_input : target_inputs) {
            const std::string input_name = io::trim(raw_input);
            if (input_name.empty()) {
                throw std::runtime_error("BMI forcing error: identity variables_names_map "
                                         "encountered an empty BMI input variable name");
            }
            append_validated_coupling(input_name, input_name);
        }
        return;
    }

    // Explicit maps couple forcing source variables to configured BMI inputs
    for (const std::pair<const std::string, std::string>& map_entry : variables_names_map) {
        append_validated_coupling(map_entry.second, map_entry.first);
    }
}

// Initialize pipeline modules from one decoded config
void BmiPipeline::initialize(const BmiPipelineConfig& pipeline_config) {
    if (pipeline_config.module_configs_.size() != 1) {
        io::throw_config_error("BMI pipeline config error: ",
                               "bmi_multi modules must contain one entry");
    }
    output_module_ = pipeline_config.output_module_;

    // Add the configured model module before attaching optional forcing
    const BmiModuleConfig& module_config = pipeline_config.module_configs_.front();

    // The model module defines whether task CSV output is disabled or selected
    if (module_config.output_variables_) {
        output_variables_ = *module_config.output_variables_;
    }
    output_interval_ = pipeline_config.output_interval_;
    const double time_step_tolerance_seconds = pipeline_config.time_step_tolerance_seconds_;
    const std::string forcing_file = io::trim(pipeline_config.forcing_file_);
    if (!std::isfinite(time_step_tolerance_seconds) || time_step_tolerance_seconds < 0.0) {
        throw std::runtime_error(
            "BMI pipeline config error: time_step_tolerance_seconds must be finite "
            "and non-negative");
    }
    if (output_interval_ &&
        (!std::isfinite(*output_interval_) || *output_interval_ <= 0.0)) {
        throw std::runtime_error(
            "BMI pipeline config error: output_interval must be finite and positive");
    }

    // Add a single model module to the pipeline and return its module index
    const std::size_t model_module_index = add_model_module(module_config);

    // The output module must be a valid index into the modules_ vector
    if (output_module_ >= modules_.size()) {
        throw std::runtime_error("BMI pipeline error: output module is invalid");
    }

    // Add the optional forcing module after the model module so couplings can be built
    std::size_t forcing_module_index = 0;
    const bool has_forcing =
        add_forcing_module(pipeline_config.forcing_format_, forcing_file,
                           pipeline_config.forcing_feature_id_variable_,
                           pipeline_config.feature_id_, time_step_tolerance_seconds,
                           model_module_index, forcing_module_index);

    // Initialize the model before querying its metadata from optional forcing
    modules_.at(model_module_index)->Initialize(module_config.config_file_);
    ++initialized_module_count_;
    if (has_forcing) {
        modules_.at(forcing_module_index)->Initialize(forcing_file);
        ++initialized_module_count_;
    }

    // Convert the time step tolerance from seconds to the exposed model clock unit
    time_step_tolerance_ =
        time_step_tolerance_seconds /
        seconds_per_time_unit(output_model().GetTimeUnits(), "BMI pipeline error: ");

    // An enabled empty selection resolves to every output exposed after initialization
    if (module_config.output_variables_ && module_config.output_variables_->empty()) {
        output_variables_ = output_model().GetOutputVarNames();
    }

    // Distinct native updates must remain distinguishable by the configured tolerance
    const double time_step = output_model().GetTimeStep();
    if (!std::isfinite(time_step) || time_step <= 0.0) {
        throw std::runtime_error(
            "BMI pipeline error: output model time step must be finite and positive");
    }
    if (time_step_tolerance_ >= time_step / 2.0) {
        throw std::runtime_error(
            "BMI pipeline error: time_step_tolerance_seconds must be less than half "
            "the output model time step");
    }

    if (has_forcing) {
        // Couplings are built after initialization so BMI metadata is available
        build_couplings(forcing_module_index, model_module_index,
                        module_config.variables_names_map_);
    }
}

// Update each module in order, applying couplings before each target module update
void BmiPipeline::update() {
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index) {
        for (const BmiCoupling& coupling : couplings_) {
            if (coupling.target_module_index() == module_index) {
                coupling.apply(*modules_.at(coupling.source_module_index()),
                               *modules_.at(coupling.target_module_index()));
            }
        }
        modules_.at(module_index)->Update();
    }
    ++completed_update_count_;
}

// Call update until the output model time is within tolerance of the requested time
void BmiPipeline::update_until(double time) {
    double remaining_time = time - output_model().GetCurrentTime();
    while (remaining_time > time_step_tolerance_) {
        update();
        remaining_time = time - output_model().GetCurrentTime();
    }

    // Reject an update that overshoots the target beyond the configured tolerance
    if (remaining_time < -time_step_tolerance_) {
        throw std::runtime_error(
            "BMI pipeline error: update passed the requested target time");
    }
}

void BmiPipeline::finalize() {
    if (initialized_module_count_ == 0) {
        return;
    }

    // Finalize every module before rethrowing the first finalization error
    std::exception_ptr first_exception;

    // Finalize modules in update order so lifecycle order is explicit
    for (std::size_t module_index = 0; module_index < initialized_module_count_;
         ++module_index) {
        try {
            modules_.at(module_index)->Finalize();
        } catch (...) {
            if (!first_exception) {
                first_exception = std::current_exception();
            }
        }
    }

    // Mark the schedule as finalized while preserving module ownership
    initialized_module_count_ = 0;

    // Rethrow the first exception encountered during finalization
    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

// Return the selected model used for task output
::bmi::Bmi& BmiPipeline::output_model() {
    return *modules_.at(output_module_);
}

} // namespace miaow::bmi
