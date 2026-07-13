# Choose the local C++ model path, fetching the pinned source when absent
miaow_resolve_model_root(cxx_heat_model_root
    MODEL_ROOT "${MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT}"
    MODEL_LOCAL_SUBDIR "bmi-example-cxx"
    MODEL_LABEL "dynamic_bmi_cpp_heat"
)

if(NOT EXISTS "${cxx_heat_model_root}")
    include(FetchContent)
    FetchContent_Populate(
        bmi_example_cxx
        GIT_REPOSITORY https://github.com/csdms/bmi-example-cxx.git
        GIT_TAG "cd6347165021e607204820420eb8af14df26fa59"
        SOURCE_DIR "${cxx_heat_model_root}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/bmi_example_cxx-build"
    )
endif()

# Fail during configuration when the selected checkout has the wrong layout
if(NOT EXISTS "${cxx_heat_model_root}/heat/heat.cxx")
    message(FATAL_ERROR
        "MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT must point to a "
        "csdms/bmi-example-cxx checkout")
endif()

# Build the shared library that exports the dynamic BMI object interface
miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_dynamic_cpp_heat_plugin
    SOURCES
        "${cxx_heat_model_root}/heat/heat.cxx"
        "${cxx_heat_model_root}/heat/bmi_heat.cxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/create_destroy_bmi_heat.cpp"
    INCLUDE_DIRS
        "${cxx_heat_model_root}/heat"
    USE_MIAOW_BMI_INCLUDE
)
