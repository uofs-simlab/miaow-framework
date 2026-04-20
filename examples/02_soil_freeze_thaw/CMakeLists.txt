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

target_compile_definitions(${MIAOW_EXECUTABLE_TARGET_RESOLVED} PRIVATE
    MIAOW_SOIL_FREEZE_THAW_EXAMPLE_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw
        EXECUTABLE_TARGET "${MIAOW_EXECUTABLE_TARGET_RESOLVED}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "\\[2\\] succeeded: BMI workflow complete"
        CLI_ARGS
            --output_root
            "${CMAKE_CURRENT_BINARY_DIR}/output_smoke"
            --max_workers
            2
            --task_request_timeout
            60
            --job_request_timeout
            600
    )

    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw_forcing_matched
        EXECUTABLE_TARGET "${MIAOW_EXECUTABLE_TARGET_RESOLVED}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "\\[0\\] succeeded: BMI workflow complete"
        CLI_ARGS
            --max_workers
            1
            --task_request_timeout
            60
            --job_request_timeout
            600
    )

    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw_forcing_exhausted
        EXECUTABLE_TARGET "${MIAOW_EXECUTABLE_TARGET_RESOLVED}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "\\[1\\] failed: Forcing alignment error: forcing series exhausted at step 24 \\(updates: 24\\)"
        CLI_ARGS
            --max_workers
            1
            --task_request_timeout
            60
            --job_request_timeout
            600
    )

    set(soil_freeze_thaw_layout_cli_args
        --output_root
        "${CMAKE_CURRENT_BINARY_DIR}/output_metadata"
        --max_workers
        1
        --task_request_timeout
        60
        --job_request_timeout
        600
    )
    list(JOIN soil_freeze_thaw_layout_cli_args ";" soil_freeze_thaw_layout_cli_args_serialized)

    add_test(
        NAME miaow_example_02_soil_freeze_thaw_metadata_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${MIAOW_EXECUTABLE_TARGET_RESOLVED}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/output_metadata
            "-DEXECUTABLE_ARGS=${soil_freeze_thaw_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_metadata_layout.cmake
    )
    set_tests_properties(miaow_example_02_soil_freeze_thaw_metadata_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()
