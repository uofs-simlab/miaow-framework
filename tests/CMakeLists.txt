# Check saved example results without adding test logic to the example programs
add_executable(miaow_example_tests example_regression_checks.cpp)
target_compile_features(miaow_example_tests PRIVATE cxx_std_20)
target_link_libraries(miaow_example_tests PRIVATE
    nlohmann_json::nlohmann_json Boost::unit_test_framework)
