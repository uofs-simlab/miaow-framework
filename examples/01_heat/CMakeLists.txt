cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(heat)

include("${MIAOW_ROOT}/cmake/miaow_bmi_model.cmake")

# Build csdms/bmi-example-cxx as a static model library and link it directly
# into the example executable (no dynamic plugin loading).
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

# Inject the example dir so main.cpp can reference its bundled config files
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_HEAT_EXAMPLE_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_workflow_test(
        TEST_NAME miaow_example_01_heat
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "\\[0\\] succeeded: BMI workflow complete(.|\\n)*\\[1\\] succeeded: BMI workflow complete"
        CLI_ARGS
            --max_workers
            2
            --task_request_timeout
            30
            --job_request_timeout
            300
    )
endif()
