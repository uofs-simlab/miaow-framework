#include "bmi/internal.hpp"

// Entry point for subprocess execution, launched via Boost.Process from run_subprocess_task
int main(int argc, char* argv[]) {
    return miaow::bmi::run_subprocess_cli(argc, argv);
}
