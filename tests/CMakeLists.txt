# Tests for the MIAOW framework (see documentation for details)
#
# Check saved job records from examples and framework drivers
add_executable(miaow_job_record_checks job_record_checks.cpp)
target_compile_features(miaow_job_record_checks PRIVATE cxx_std_20)
target_link_libraries(miaow_job_record_checks PRIVATE
    nlohmann_json::nlohmann_json Boost::unit_test_framework)

# Exercise unit lifetimes through the same driver and saved-record workflow
add_executable(miaow_simulation_unit_lifecycle_driver simulation_unit_lifecycle_driver.cpp)
target_compile_features(miaow_simulation_unit_lifecycle_driver PRIVATE cxx_std_20)
target_link_libraries(miaow_simulation_unit_lifecycle_driver PRIVATE
    miaow nlohmann_json::nlohmann_json)

include("${PROJECT_SOURCE_DIR}/cmake/miaow_example.cmake")
foreach(scenario removal_with_queued_work replacement_after_failure)
    foreach(variant IN ITEMS "" "_concurrent" "_batch_synchronous"
            "_batch_synchronous_concurrent")
        miaow_add_example_test(
            TEST_NAME miaow_simulation_unit_${scenario}${variant}
            EXECUTABLE_TARGET miaow_simulation_unit_lifecycle_driver
            REGRESSION simulation_unit_${scenario}
            CASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cases/unit_${scenario}${variant}.json"
        )
    endforeach()
endforeach()
