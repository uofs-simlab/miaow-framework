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
    // Check that we're running from the expected directory for proper relative paths
    if (!miaow::ensure_expected_run_dir("miaow_noah_owp", MIAOW_NOAH_OWP_RUN_DIR)) {
        return 1;
    }

    // Initialize realization config path
    std::string realization_path;
    miaow::RealizationConfig realization;
    try {
        realization_path = miaow::parse_realization_cli_path(
            argc, argv, "miaow_noah_owp", "realization.json");
        realization = miaow::read_realization_config(realization_path);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    std::cout << "Parsed realization with "
              << realization.subdomains_.size()
              << " subdomain(s)\n";

    // Load the Noah-OWP BMI plugin to verify that it can be loaded successfully
    const std::string plugin_path = MIAOW_NOAH_OWP_PLUGIN_PATH;
    try {
        miaow::DynamicFortranBmiModel noah_owp_plugin(plugin_path);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }

    std::cout << "Loaded Noah-OWP BMI plugin: " << plugin_path << "\n";
    return 0;
}
