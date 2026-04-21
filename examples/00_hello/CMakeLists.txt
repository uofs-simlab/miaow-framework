cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_hello_example CXX)
endif()

if(NOT DEFINED MIAOW_ROOT)
    get_filename_component(miaow_root_default "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(MIAOW_ROOT "${miaow_root_default}" CACHE PATH
        "Path to the miaow-framework source root")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EXECUTABLE_TARGET "miaow_hello" CACHE STRING
        "Executable target name for this example")
else()
    set(EXECUTABLE_TARGET "miaow_hello")
endif()

if(NOT TARGET miaow)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
    add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
endif()

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)

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
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "succeeded: hello(.|\\n)*succeeded: cats(.|\\n)*succeeded: hello again(.|\\n)*succeeded: more cats"
    )

    set(hello_layout_cli_args
        --output_root
        "${CMAKE_CURRENT_BINARY_DIR}/output"
    )
    list(JOIN hello_layout_cli_args ";" hello_layout_cli_args_serialized)

    add_test(
        NAME miaow_example_00_hello_metadata_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/output
            "-DEXECUTABLE_ARGS=${hello_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_metadata_layout.cmake
    )
    set_tests_properties(miaow_example_00_hello_metadata_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()
