cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_heat_example LANGUAGES CXX)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(heat)

# Load the helper that builds a static BMI model with an example executable
include("${MIAOW_ROOT}/cmake/miaow_bmi_model.cmake")

# Allow a local checkout to override the default third_party source
set(MIAOW_HEAT_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-cxx source tree")

# Link the C++ heat model directly without dynamic plugin loading
miaow_add_bmi_model(
    MODEL_ROOT "${MIAOW_HEAT_MODEL_ROOT}"
    EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    MODEL_LOCAL_SUBDIR bmi-example-cxx
    MODEL_GIT_REPOSITORY "https://github.com/csdms/bmi-example-cxx.git"
    MODEL_GIT_TAG "cd6347165021e607204820420eb8af14df26fa59"
    EXECUTABLE_SOURCES main.cpp
    MODEL_SOURCES
        heat/heat.cxx
        heat/bmi_heat.cxx
    MODEL_INCLUDE_DIRS
        heat
    USE_MIAOW_BMI_INCLUDE
)

target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_HEAT_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_01_heat
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        EXTERNAL_MODEL
    )
endif()
