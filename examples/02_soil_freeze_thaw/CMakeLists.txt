cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_soil_freeze_thaw_example LANGUAGES CXX)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(soil_freeze_thaw)

# Load the helper that builds a static BMI model with an example executable
include("${MIAOW_ROOT}/cmake/miaow_bmi_model.cmake")

# Allow a local checkout to override the default third_party source
set(MIAOW_SOIL_FREEZE_THAW_MODEL_ROOT "" CACHE PATH
    "Path to a local NOAA-OWP/SoilFreezeThaw source tree")

# Link SoilFreezeThaw directly without dynamic plugin loading
miaow_add_bmi_model(
    MODEL_ROOT "${MIAOW_SOIL_FREEZE_THAW_MODEL_ROOT}"
    EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    MODEL_LOCAL_SUBDIR SoilFreezeThaw
    MODEL_GIT_REPOSITORY "https://github.com/NOAA-OWP/SoilFreezeThaw.git"
    MODEL_GIT_TAG "d7a4bdbbe773d79c5d5b81a1255e5288cec01e5b"
    EXECUTABLE_SOURCES main.cpp
    MODEL_SOURCES
        src/bmi_soil_freeze_thaw.cxx
        src/soil_freeze_thaw.cxx
    MODEL_INCLUDE_DIRS
        include
        .
    MODEL_COMPILE_DEFINITIONS BMI_ACTIVE
)

target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_SOIL_FREEZE_THAW_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        EXTERNAL_MODEL
    )
endif()
