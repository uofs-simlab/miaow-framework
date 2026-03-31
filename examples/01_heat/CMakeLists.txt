cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_heat_example CXX)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_bmi_model.cmake")

miaow_add_bmi_model(
    EXAMPLE_NAME heat
    MIAOW_ROOT "${MIAOW_ROOT}"
    MODEL_ROOT "${MODEL_ROOT}"
    EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
    MODEL_TARGET "${MODEL_TARGET}"
    MODEL_LOCAL_SUBDIR bmi-example-cxx
    MODEL_GIT_REPOSITORY "https://github.com/csdms/bmi-example-cxx.git"
    MODEL_GIT_TAG "cd6347165021e607204820420eb8af14df26fa59"
    MODEL_FETCH_NAME bmi_example_cxx
    EXECUTABLE_SOURCES main.cpp
    MODEL_SOURCES
        heat/heat.cxx
        heat/bmi_heat.cxx
    MODEL_INCLUDE_DIRS
        heat
    USE_MIAOW_BMI_INCLUDE
)

if(BUILD_TESTING)
    miaow_add_workflow_test(
        TEST_NAME miaow_example_01_heat
        EXECUTABLE_TARGET "${MIAOW_EXECUTABLE_TARGET_RESOLVED}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "\\[0\\] succeeded: BMI workflow complete(.|\\n)*\\[1\\] succeeded: BMI workflow complete"
        CLI_ARGS
            --config
            "${CMAKE_CURRENT_LIST_DIR}/config_400x400.txt"
            "${CMAKE_CURRENT_LIST_DIR}/config_800x800.txt"
            --max_workers
            2
            --task_request_timeout
            30
            --job_request_timeout
            300
    )
endif()
