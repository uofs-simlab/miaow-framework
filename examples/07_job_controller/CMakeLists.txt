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
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_07_job_controller
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    )
endif()
