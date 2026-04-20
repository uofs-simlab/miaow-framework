#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "io/realization.hpp"

namespace {

std::string parse_cli_realization_path(int argc, char* argv[]) {
    std::string realization_path = "realization.json";
    int index = 1;
    while (index < argc) {
        const std::string option_name = argv[index];
        if (option_name == "--realization") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Error: --realization requires a value");
            }
            realization_path = argv[index + 1];
            index += 2;
            continue;
        }
        if (option_name == "--help") {
            std::cout << "Usage: miaow_noah_owp [--realization <path>]\n";
            std::exit(0);
        }
        throw std::runtime_error("Error: unrecognized argument: " + option_name);
    }
    return realization_path;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const std::string realization_path =
            parse_cli_realization_path(argc, argv);
        const miaow::io::RealizationConfig realization =
            miaow::io::read_realization_config(realization_path);

        std::cout << "Parsed realization with "
                  << realization.subdomains_.size()
                  << " subdomain(s)\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
        return 1;
    }
    return 0;
}
