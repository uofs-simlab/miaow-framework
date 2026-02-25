/**
 * This example demonstrates how to use the MIAOW framework to run a simple BMI model of 
 * heat diffusion on a plate. The example includes a custom BMI implementation (BmiHeat) 
 * that wraps a simple heat diffusion model, and a main program that sets up the MIAOW 
 * runtime to execute the BMI workflow with a given configuration file.
 */

#include <iostream>
#include <string>

#include "bmi_heat.hxx"     // defines the BmiHeat class that implements the BMI interface
#include "miaow.hpp"        // Core MIAOW runtime API
#include "miaow_bmi.hpp"    // MIAOW API for running BMI models as simulation units

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: miaow_heat config.txt\n";
        return 1;
    }

    miaow::RuntimeOptions runtime_options;
    miaow::Runtime runtime =
        miaow::make_runtime<miaow::BmiSimulationUnit<BmiHeat>>(runtime_options);
    std::cout << runtime.run_workflow(argv[1]) << "\n";
    return 0;
}
