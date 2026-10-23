# Load the helper that builds a static BMI model with an example executable
include("${PROJECT_SOURCE_DIR}/cmake/miaow_bmi_model.cmake")

# Allow a local checkout to override the default third_party source
set(MIAOW_HEAT_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-cxx source tree")

# Link the C++ heat model statically
miaow_add_bmi_model(
    MODEL_ROOT "${MIAOW_HEAT_MODEL_ROOT}"
    EXECUTABLE_TARGET miaow_heat
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

target_compile_definitions(miaow_heat PRIVATE
    MIAOW_HEAT_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_01_heat
        EXECUTABLE_TARGET miaow_heat
        REGRESSION heat
        EXTERNAL_MODEL
    )
endif()
