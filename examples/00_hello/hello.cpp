#include <iostream>
#include <string>

#include "miaow.hpp"

int main(int argc, char* argv[]) {
    std::string config_file = "config.txt";
    if (argc > 1) {
        config_file = argv[1];
    }

    // Call into MIAOW with a config path that can later be passed to BMI Initialize.
    std::cout << miaow::miaow_run(config_file) << "\n";
    return 0;
}
