#include "miaow_bmi.hpp"

#include <map>
#include <string>
#include <vector>

#include <caf/config_value.hpp>

#include "miaow_io.hpp"

namespace miaow {

namespace {

SimulationUnitId register_unit(SimulationUnitRegistry& registry,
                               std::map<std::string, SimulationUnitId>& unit_ids,
                               const std::string& library_file,
                               BmiExecutionMode execution_mode) {
    const std::map<std::string, SimulationUnitId>::const_iterator existing_unit =
        unit_ids.find(library_file);
    if (existing_unit != unit_ids.end()) {
        return existing_unit->second;
    }

    // Realization-driven BMI execution currently supports Fortran modules
    const SimulationUnitId unit_id =
        registry.add<BmiSimulationUnit<DynamicFortranBmi>>(
            library_file,
            execution_mode);
    unit_ids.emplace(library_file, unit_id);
    return unit_id;
}

} // namespace

std::vector<Task> build_bmi_realization_tasks(const RealizationConfig& realization,
                                              SimulationUnitRegistry& registry,
                                              BmiExecutionMode execution_mode) {
    std::map<std::string, SimulationUnitId> unit_ids;

    // Register all dynamic BMI units before the runtime is constructed
    for (const RealizationFeature& feature : realization.features_) {
        register_unit(registry, unit_ids, feature.library_file_, execution_mode);
    }

    // Build one task for each feature in realization order
    std::vector<Task> tasks;
    tasks.reserve(realization.features_.size());
    for (const RealizationFeature& feature : realization.features_) {
        const SimulationUnitId unit_id = unit_ids.at(feature.library_file_);

        // MIAOW-managed CSV forcing is passed as the second BMI task argument
        tasks.push_back(Task(
            unit_id,
            std::vector<caf::config_value>{
                caf::config_value{feature.init_config_path_},
                make_forcing_config(
                    feature.forcing_file_,
                    feature.variables_names_map_),
            }));
    }

    return tasks;
}

} // namespace miaow
