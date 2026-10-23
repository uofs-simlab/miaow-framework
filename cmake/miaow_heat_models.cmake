include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/miaow_example.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/miaow_dynamic_bmi_plugin.cmake")

# Build the pinned CSDMS heat plugins for the dynamic BMI example
function(miaow_add_fortran_heat_model)
    if(TARGET miaow_fortran_heat_model)
        return()
    endif()
    set(MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT "" CACHE PATH
        "Path to a local csdms/bmi-example-fortran source tree")

    # Choose the local Fortran model path, fetching the pinned source when absent
    miaow_resolve_model_root(fortran_heat_model_root
        MODEL_ROOT "${MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT}"
        MODEL_LOCAL_SUBDIR "bmi-example-fortran"
        MODEL_LABEL "dynamic_bmi_fortran_heat"
    )

    if(NOT EXISTS "${fortran_heat_model_root}")
        include(FetchContent)
        FetchContent_Populate(
            bmi_example_fortran
            GIT_REPOSITORY https://github.com/csdms/bmi-example-fortran.git
            GIT_TAG "74df8d888433c43fc7f7b96109a445bead9972d2"
            SOURCE_DIR "${fortran_heat_model_root}"
            BINARY_DIR "${PROJECT_BINARY_DIR}/heat-models/fortran-source-build"
        )
    endif()

    # Validate the Fortran checkout before defining build targets
    if(NOT EXISTS "${fortran_heat_model_root}/heat/heat.f90")
        message(FATAL_ERROR
            "MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT must point to a "
            "csdms/bmi-example-fortran checkout")
    endif()

    # Replace the upstream bmif_2_0 import with the bmif_2_0_iso module
    # required by MIAOW's procedural Fortran BMI loader
    set(fortran_heat_generated_dir "${PROJECT_BINARY_DIR}/heat-models/fortran")
    file(MAKE_DIRECTORY "${fortran_heat_generated_dir}")
    file(READ
        "${fortran_heat_model_root}/bmi_heat/bmi_heat.f90"
        bmi_heat_source
    )
    string(REPLACE
        "use bmif_2_0"
        "use bmif_2_0_iso"
        bmi_heat_source
        "${bmi_heat_source}"
    )
    file(WRITE
        "${fortran_heat_generated_dir}/bmi_heat.f90"
        "${bmi_heat_source}"
    )

    # Compile the model once; plugins supply their own registration entry point
    add_library(miaow_fortran_heat_model STATIC
        "${fortran_heat_model_root}/heat/heat.f90"
        "${fortran_heat_generated_dir}/bmi_heat.f90"
    )
    set_target_properties(miaow_fortran_heat_model PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        Fortran_MODULE_DIRECTORY "${fortran_heat_generated_dir}/mod"
    )
    target_include_directories(miaow_fortran_heat_model PUBLIC
        "${fortran_heat_generated_dir}/mod")
    target_link_libraries(miaow_fortran_heat_model PUBLIC iso_c_bmi)

    miaow_add_dynamic_bmi_plugin(
        PLUGIN_TARGET miaow_dynamic_fortran_heat_plugin
        SOURCES "${PROJECT_SOURCE_DIR}/examples/03_dynamic_bmi/register_bmi_heat.f90"
    )
    target_link_libraries(miaow_dynamic_fortran_heat_plugin PRIVATE miaow_fortran_heat_model)
endfunction()

function(miaow_add_cpp_heat_model)
    if(TARGET miaow_dynamic_cpp_heat_plugin)
        return()
    endif()
    set(MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT "" CACHE PATH
        "Path to a local csdms/bmi-example-cxx source tree")

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
            BINARY_DIR "${PROJECT_BINARY_DIR}/heat-models/cpp-source-build"
        )
    endif()

    # Fail during configuration when the selected checkout has the wrong layout
    if(NOT EXISTS "${cxx_heat_model_root}/heat/heat.cxx")
        message(FATAL_ERROR
            "MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT must point to a "
            "csdms/bmi-example-cxx checkout")
    endif()

    # Build the shared library that exports the dynamic BMI object interface
    add_library(miaow_dynamic_cpp_heat_plugin SHARED
        "${cxx_heat_model_root}/heat/heat.cxx"
        "${cxx_heat_model_root}/heat/bmi_heat.cxx"
        "${PROJECT_SOURCE_DIR}/examples/03_dynamic_bmi/create_destroy_bmi_heat.cpp"
    )
    target_compile_features(miaow_dynamic_cpp_heat_plugin PRIVATE cxx_std_20)
    target_include_directories(miaow_dynamic_cpp_heat_plugin PUBLIC
        "${cxx_heat_model_root}/heat"
        "${PROJECT_SOURCE_DIR}/include/third_party/bmi"
    )
endfunction()
