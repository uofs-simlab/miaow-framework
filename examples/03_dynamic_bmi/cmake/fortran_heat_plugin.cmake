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
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/bmi_example_fortran-build"
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
set(fortran_heat_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
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

# Build the shared library with the procedural BMI bridge
miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_dynamic_fortran_heat_plugin
    SOURCES
        "${fortran_heat_model_root}/heat/heat.f90"
        "${fortran_heat_generated_dir}/bmi_heat.f90"
        "${CMAKE_CURRENT_SOURCE_DIR}/register_bmi_heat.f90"
    LINK_ISO_C_BMI
    FORTRAN_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mod"
)
