#pragma once

#include <type_traits>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// To avoid confusion with the miaow::bmi namespace, we alias the BMI API namespace here
namespace bmi_api = ::bmi;

// Public bridge from header template code to compiled BMI execution logic
TaskResult execute_bmi_model(bmi_api::Bmi& model, const Task& task);

/**
 * @brief Simulation unit that runs a complete BMI model lifecycle.
 *
 * @tparam BmiModelType BMI model type derived from bmi_api::Bmi.
 */
template <class BmiModelType>
class BmiSimulationUnit : public SimulationUnit {
public:
    static_assert(std::is_base_of_v<bmi_api::Bmi, BmiModelType>,
                  "BmiModelType must derive from bmi::Bmi");
    static_assert(std::is_default_constructible_v<BmiModelType>,
                  "BmiModelType must be default constructible");

    TaskResult execute(const Task& task) override {
        BmiModelType bmi_model;
        return execute_bmi_model(bmi_model, task);
    }
};

} // namespace miaow
