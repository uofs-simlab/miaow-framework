cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_job_controller_example LANGUAGES CXX)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(job_controller)

# Build the example and link it to the MIAOW runtime
add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_JOB_CONTROLLER_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Place the binary in the example directory for source-relative execution
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    # Build a focused regression for preserving generated tasks after a job timeout
    add_executable(miaow_job_controller_timeout_test
        timeout_test.cpp
    )
    target_compile_features(miaow_job_controller_timeout_test PRIVATE cxx_std_20)
    target_link_libraries(miaow_job_controller_timeout_test PRIVATE miaow)

    add_test(
        NAME miaow_job_controller_generated_task_timeout
        COMMAND miaow_job_controller_timeout_test
    )
    set_tests_properties(miaow_job_controller_generated_task_timeout PROPERTIES
        TIMEOUT 10
    )

    # Build a focused regression for propagating controller exceptions to JobHandle
    add_executable(miaow_job_controller_exception_test
        exception_test.cpp
    )
    target_compile_features(miaow_job_controller_exception_test PRIVATE cxx_std_20)
    target_link_libraries(miaow_job_controller_exception_test PRIVATE miaow)

    add_test(
        NAME miaow_job_controller_exception
        COMMAND miaow_job_controller_exception_test
    )
    set_tests_properties(miaow_job_controller_exception PROPERTIES
        TIMEOUT 10
    )

    # Check deterministic task messages while allowing randomized sleep durations
    string(CONCAT job_controller_pass_regex
        "succeeded: hello from task 0.*finished tasks: 0(.|\\n)*"
        "succeeded: hello from task 1.*finished tasks: 0(.|\\n)*"
        "succeeded: hello from task 2.*finished tasks: 1(.|\\n)*"
        "succeeded: hello from task 3.*finished tasks: 2(.|\\n)*"
        "succeeded: hello from task 4.*finished tasks: 3"
    )
    add_test(
        NAME miaow_example_07_job_controller
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_07_job_controller PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "${job_controller_pass_regex}"
    )

    # Check that the complete generated task collection is written to disk
    set(job_controller_layout_cli_args
        --output_root
        "${CMAKE_CURRENT_LIST_DIR}/output"
    )
    list(JOIN job_controller_layout_cli_args ";"
        job_controller_layout_cli_args_serialized)

    add_test(
        NAME miaow_example_07_job_controller_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            -DCHECK_SECOND_RUN=TRUE
            -DREQUIRE_SUCCESS=TRUE
            -DEXPECTED_TASK_COUNT=5
            "-DEXECUTABLE_ARGS=${job_controller_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_07_job_controller_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
