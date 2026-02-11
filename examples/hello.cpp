#include <iostream>

#include "miaow.hpp"

int main() {
    // Call into MIAOW, which uses CAF internally.
    miaow_init();
    std::cout << miaow_actor_hello() << "\n";
    return 0;
}
