# Build the example and link it to the MIAOW runtime
add_executable(miaow_job_controller
    main.cpp
)

target_compile_features(miaow_job_controller PRIVATE cxx_std_20)
target_link_libraries(miaow_job_controller PRIVATE miaow nlohmann_json::nlohmann_json)
target_compile_definitions(miaow_job_controller PRIVATE
    MIAOW_JOB_CONTROLLER_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_job_controller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_07_job_controller
        EXECUTABLE_TARGET miaow_job_controller
        REGRESSION controller
    )
    miaow_add_example_test(
        TEST_NAME miaow_example_07_job_controller_non_convergence
        EXECUTABLE_TARGET miaow_job_controller
        REGRESSION failure
        CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/controller_non_convergence.json"
    )
    miaow_add_example_test(
        TEST_NAME miaow_example_07_job_controller_small_values
        EXECUTABLE_TARGET miaow_job_controller
        REGRESSION controller_small_values
        CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/controller_small_values.json"
    )
endif()
