#pragma once

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "miaow_io.hpp"

// Read optional example inputs and forward the common runtime options
inline nlohmann::json read_example_config(int argc, char* argv[],
                                          miaow::RuntimeOptions& runtime_options) {
    std::string config_path;
    std::vector<char*> runtime_arguments{argv[0]};
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--config") {
            if (++index == argc) {
                throw std::runtime_error("--config requires a JSON file path");
            }
            config_path = argv[index];
        } else {
            runtime_arguments.push_back(argv[index]);
        }
    }
    miaow::parse_runtime_cli_options(static_cast<int>(runtime_arguments.size()),
                                     runtime_arguments.data(), runtime_options);
    if (config_path.empty()) {
        return nlohmann::json::object();
    }
    std::ifstream input{config_path};
    return nlohmann::json::parse(input);
}
