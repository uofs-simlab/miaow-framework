cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(hello)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_HELLO_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Drop the binary next to the source so example data (configs, realization
# files) can be referenced relative to the executable.
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_00_hello
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_00_hello PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "succeeded: hello(.|\\n)*succeeded: cats(.|\\n)*succeeded: hello again(.|\\n)*succeeded: more cats"
    )

    # Drives the example with --output_root and validates the on-disk layout
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
            "-DEXECUTABLE_ARGS=${hello_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_00_hello_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
