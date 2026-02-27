cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_hello_example CXX)
endif()

get_filename_component(MIAOW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET miaow)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
    add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
endif()

add_executable(miaow_hello
    main.cpp
)

target_link_libraries(miaow_hello PRIVATE miaow)

set(miaow_hello_output_dir "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(miaow_hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${miaow_hello_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${miaow_hello_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${miaow_hello_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${miaow_hello_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${miaow_hello_output_dir}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_00_hello
        COMMAND
            $<TARGET_FILE:miaow_hello>
    )
    set_tests_properties(miaow_example_00_hello PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "miaow!"
    )
endif()
