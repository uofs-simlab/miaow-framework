# Keep two persistent counters while allowing only one active task request
add_executable(miaow_persistent_counter main.cpp)
target_compile_features(miaow_persistent_counter PRIVATE cxx_std_20)
target_link_libraries(miaow_persistent_counter PRIVATE miaow nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_09_persistent_counter
        EXECUTABLE_TARGET miaow_persistent_counter
        REGRESSION persistent_counter
        CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/counter_success.json"
    )
endif()
