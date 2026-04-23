/**
 * This example demonstrates how to load the Noah-OWP BMI plugin and parse a realization
 * config file for the Noah-OWP example. 
 */
#include <exception>
#include <iostream>
#include <string>

#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"

int main(int argc, char* argv[]) {
    try {
        // Initialize realization options
        const std::string realization_path = miaow::parse_realization_cli_path(
            argc, argv, "miaow_noah_owp", "realization.json");
        const miaow::RealizationConfig realization =
            miaow::read_realization_config(realization_path);
        std::cout << "Parsed realization with "
                  << realization.subdomains_.size()
                  << " subdomain(s)\n";

        // Load the Noah-OWP BMI plugin to verify that it can be loaded successfully
        const std::string plugin_path = MIAOW_NOAH_OWP_PLUGIN_PATH;
        miaow::DynamicFortranBmiModel noah_owp_plugin(plugin_path);
        std::cout << "Loaded Noah-OWP BMI plugin: " << plugin_path << "\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    return 0;
}
