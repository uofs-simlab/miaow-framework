cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(soil_freeze_thaw)

include("${MIAOW_ROOT}/cmake/miaow_bmi_model.cmake")

set(MIAOW_SOIL_FREEZE_THAW_MODEL_ROOT "" CACHE PATH
    "Path to a local NOAA-OWP/SoilFreezeThaw source tree")
if(MIAOW_SOIL_FREEZE_THAW_MODEL_ROOT)
    set(soil_freeze_thaw_model_root_override "${MIAOW_SOIL_FREEZE_THAW_MODEL_ROOT}")
else()
    set(soil_freeze_thaw_model_root_override "")
endif()

# Build NOAA-OWP/SoilFreezeThaw as a static model library and link it directly
# into the example executable (no dynamic plugin loading).
set(soil_freeze_thaw_model_target "${EXECUTABLE_TARGET}_model")
miaow_add_bmi_model(
    EXAMPLE_NAME soil_freeze_thaw
    MIAOW_ROOT "${MIAOW_ROOT}"
    MODEL_ROOT "${soil_freeze_thaw_model_root_override}"
    EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    MODEL_TARGET "${soil_freeze_thaw_model_target}"
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

miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "SoilFreezeThaw"
    TARGET_DIR "${MODEL_ROOT}"
)

target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_SOIL_FREEZE_THAW_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_02_soil_freeze_thaw_extern_symlink
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${CMAKE_CURRENT_LIST_DIR}/extern/SoilFreezeThaw
            -DEXPECT_TARGET=${MODEL_ROOT}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "\\[2\\] succeeded: BMI workflow complete"
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
    set_tests_properties(miaow_example_02_soil_freeze_thaw PROPERTIES
        RESOURCE_LOCK miaow_example_02_soil_freeze_thaw_output
    )

    miaow_add_workflow_test(
        TEST_NAME miaow_example_02_soil_freeze_thaw_forcing_matched
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
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
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
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

    # Drives the example with --output_root and validates the on-disk
    # run/job/task metadata layout via a shared CMake script.
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
    list(JOIN soil_freeze_thaw_layout_cli_args ";" soil_freeze_thaw_layout_cli_args_serialized)

    add_test(
        NAME miaow_example_02_soil_freeze_thaw_metadata_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            "-DEXECUTABLE_ARGS=${soil_freeze_thaw_layout_cli_args_serialized}"
            -P
            ${MIAOW_ROOT}/cmake/check_metadata_layout.cmake
    )
    set_tests_properties(miaow_example_02_soil_freeze_thaw_metadata_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_02_soil_freeze_thaw_output
    )
endif()
