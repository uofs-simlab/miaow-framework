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
    set(EXAMPLE_EXECUTABLE_TARGET "miaow_hello" CACHE STRING
        "Executable target name for this example")
else()
    set(EXAMPLE_EXECUTABLE_TARGET "miaow_hello")
endif()

if(NOT TARGET miaow)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
    add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
endif()

add_executable(${EXAMPLE_EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXAMPLE_EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXAMPLE_EXECUTABLE_TARGET} PRIVATE miaow)

set_target_properties(${EXAMPLE_EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_00_hello
        COMMAND
            $<TARGET_FILE:${EXAMPLE_EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_00_hello PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "hello(.|\\n)*cats(.|\\n)*hello again(.|\\n)*more cats"
    )
endif()
