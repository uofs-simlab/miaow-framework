# Build the example and link it to the MIAOW runtime
add_executable(miaow_hello
    main.cpp
)

target_compile_features(miaow_hello PRIVATE cxx_std_20)
target_link_libraries(miaow_hello PRIVATE miaow nlohmann_json::nlohmann_json)
target_compile_definitions(miaow_hello PRIVATE
    MIAOW_HELLO_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_00_hello
        EXECUTABLE_TARGET miaow_hello
        REGRESSION hello
    )
    # Successful empty submissions use the same shared job-record fixture
    miaow_add_example_test(
        TEST_NAME miaow_example_00_hello_empty
        EXECUTABLE_TARGET miaow_hello
        REGRESSION hello_empty
        CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/hello_empty.json"
    )

    # Each JSON case defines the failure and any subsequent recovery work
    foreach(scenario missing_message unknown_executor duplicate_feature_id
                     feature_id_slash feature_id_backslash task_timeout job_timeout
                     invalid_max_workers invalid_task_timeout invalid_job_timeout)
        miaow_add_example_test(
            TEST_NAME miaow_example_00_hello_${scenario}
            EXECUTABLE_TARGET miaow_hello
            REGRESSION expected_failure
            CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/hello_${scenario}.json"
        )
    endforeach()
endif()
