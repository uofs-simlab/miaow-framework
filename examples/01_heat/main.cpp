#include <iostream>
#include <memory>
#include <string>

#include "bmi_heat.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: miaow_heat config.txt\n";
        return 1;
    }

    std::shared_ptr<miaow::SimulationUnit> simulation_unit =
        std::make_shared<miaow::BmiSimulationUnit>([]() -> std::unique_ptr<bmi::Bmi> {
            return std::make_unique<BmiHeat>();
        });

    miaow::RuntimeOptions runtime_options;
    miaow::Runtime runtime(simulation_unit, runtime_options);
    std::cout << runtime.run_workflow(argv[1]) << "\n";
    return 0;
}
