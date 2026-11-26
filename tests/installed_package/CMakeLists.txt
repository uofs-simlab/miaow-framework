cmake_minimum_required(VERSION 3.30)
project(miaow_installed_package_test LANGUAGES CXX)

find_package(miaow CONFIG REQUIRED)

add_executable(miaow_installed_package_smoke main.cpp)
target_link_libraries(miaow_installed_package_smoke PRIVATE miaow::miaow)

enable_testing()
add_test(NAME miaow_installed_package
    COMMAND miaow_installed_package_smoke "${MIAOW_INSTALLED_WORKER}")
set_tests_properties(miaow_installed_package PROPERTIES TIMEOUT 30)
