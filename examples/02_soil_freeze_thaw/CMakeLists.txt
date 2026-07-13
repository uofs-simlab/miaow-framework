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
    # Run the workflow while locking its shared output directory
    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_02_soil_freeze_thaw_output
        CLI_ARGS
            --output_root
            "${CMAKE_CURRENT_LIST_DIR}/output"
            --max_workers
            2
            --task_request_timeout
            60
            --job_request_timeout
            600
    )
    # Run again with one worker and validate the expected result files
    set(soil_freeze_thaw_layout_cli_args
        --output_root
        "${CMAKE_CURRENT_LIST_DIR}/output"
        --max_workers
        1
        --task_request_timeout
        60
        --job_request_timeout
        600
    )
    list(JOIN soil_freeze_thaw_layout_cli_args ";"
        soil_freeze_thaw_layout_cli_args_serialized)
    set(soil_freeze_thaw_expected_result_paths
        "task_0.json"
        "task_2.json"
        "results/task_0.csv"
        "results/task_2.csv"
    )
    list(JOIN soil_freeze_thaw_expected_result_paths ";"
        soil_freeze_thaw_result_paths_arg)

    add_test(
        NAME miaow_example_02_soil_freeze_thaw_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            "-DEXECUTABLE_ARGS=${soil_freeze_thaw_layout_cli_args_serialized}"
            "-DEXPECTED_RESULT_PATHS=${soil_freeze_thaw_result_paths_arg}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_02_soil_freeze_thaw_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_02_soil_freeze_thaw_output
    )
endif()
