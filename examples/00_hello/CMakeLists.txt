cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_hello_example LANGUAGES CXX)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(hello)

# Build the example and link it to the MIAOW runtime
add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_HELLO_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Place the binary beside the source so relative example paths remain stable
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    # Check the expected task results in the order printed by the example
    string(CONCAT hello_pass_regex
        "succeeded: hello(.|\\n)*"
        "succeeded: cats(.|\\n)*"
        "succeeded: hello again(.|\\n)*"
        "succeeded: more cats"
    )
    add_test(
        NAME miaow_example_00_hello
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_00_hello PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "${hello_pass_regex}"
    )

    # Run twice with file output and validate both run directories
    set(hello_layout_cli_args
        --output_root
        "${CMAKE_CURRENT_LIST_DIR}/output"
    )
    list(JOIN hello_layout_cli_args ";" hello_layout_cli_args_serialized)

    add_test(
        NAME miaow_example_00_hello_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            -DCHECK_SECOND_RUN=TRUE
            -DREQUIRE_SUCCESS=TRUE
            "-DEXECUTABLE_ARGS=${hello_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_00_hello_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
