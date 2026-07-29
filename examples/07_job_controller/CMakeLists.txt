# Build the example and link it to the MIAOW runtime
add_executable(miaow_job_controller
    main.cpp
)

target_compile_features(miaow_job_controller PRIVATE cxx_std_20)
target_link_libraries(miaow_job_controller PRIVATE miaow)
target_compile_definitions(miaow_job_controller PRIVATE
    MIAOW_JOB_CONTROLLER_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_job_controller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_07_job_controller
        EXECUTABLE_TARGET miaow_job_controller
    )
endif()
