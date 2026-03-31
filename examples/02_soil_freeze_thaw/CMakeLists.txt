cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_soil_freeze_thaw_example CXX)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_bmi_model.cmake")

miaow_add_bmi_model(
    EXAMPLE_NAME soil_freeze_thaw
    MIAOW_ROOT "${MIAOW_ROOT}"
    MODEL_ROOT "${MODEL_ROOT}"
    EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    MODEL_TARGET "${MODEL_TARGET}"
    MODEL_LOCAL_SUBDIR SoilFreezeThaw
    MODEL_GIT_REPOSITORY "https://github.com/NOAA-OWP/SoilFreezeThaw.git"
    MODEL_GIT_TAG "d7a4bdbbe773d79c5d5b81a1255e5288cec01e5b"
    MODEL_FETCH_NAME soil_freeze_thaw
    EXECUTABLE_SOURCES main.cpp
    MODEL_SOURCES
        src/bmi_soil_freeze_thaw.cxx
        src/soil_freeze_thaw.cxx
    MODEL_INCLUDE_DIRS
        include
        .
    MODEL_COMPILE_DEFINITIONS BMI_ACTIVE
)

if(BUILD_TESTING)
    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw
        EXECUTABLE_TARGET "${MIAOW_EXECUTABLE_TARGET_RESOLVED}"
        WORKING_DIRECTORY "${MIAOW_MODEL_ROOT_RESOLVED}"
        PASS_REGULAR_EXPRESSION "\\[0\\] succeeded: BMI workflow complete"
        CLI_ARGS
            --config
            "${MIAOW_MODEL_ROOT_RESOLVED}/configs/laramie_config_standalone.txt"
            --max_workers
            1
            --task_request_timeout
            60
            --job_request_timeout
            600
    )
endif()
