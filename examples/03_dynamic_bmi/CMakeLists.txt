cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(dynamic_bmi Fortran)

include("${MIAOW_ROOT}/cmake/miaow_dynamic_bmi_plugin.cmake")

set(MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-cxx source tree")
set(MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-fortran source tree")

# Resolve/fetch the CSDMS C++ heat reference model
if(MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT)
    set(cxx_heat_model_root_override "${MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT}")
else()
    set(cxx_heat_model_root_override "")
endif()
miaow_resolve_model_root(cxx_heat_model_root
    MODEL_ROOT "${cxx_heat_model_root_override}"
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

if(NOT EXISTS "${cxx_heat_model_root}/heat/heat.cxx")
    message(FATAL_ERROR
        "MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT must point to a csdms/bmi-example-cxx checkout")
endif()

# Resolve/fetch the CSDMS Fortran heat reference model
if(MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT)
    set(fortran_heat_model_root_override "${MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT}")
else()
    set(fortran_heat_model_root_override "")
endif()
miaow_resolve_model_root(fortran_heat_model_root
    MODEL_ROOT "${fortran_heat_model_root_override}"
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

if(NOT EXISTS "${fortran_heat_model_root}/heat/heat.f90")
    message(FATAL_ERROR
        "MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT must point to a csdms/bmi-example-fortran checkout")
endif()

miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "bmi-example-cxx"
    TARGET_DIR "${cxx_heat_model_root}"
)
miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "bmi-example-fortran"
    TARGET_DIR "${fortran_heat_model_root}"
)

miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_dynamic_cpp_heat_plugin
    SOURCES
        "${cxx_heat_model_root}/heat/heat.cxx"
        "${cxx_heat_model_root}/heat/bmi_heat.cxx"
        register_bmi_heat.cpp
    INCLUDE_DIRS
        "${cxx_heat_model_root}/heat"
    USE_MIAOW_BMI_INCLUDE
)

# csdms/bmi-example-fortran ships bmi_heat.f90 against the standard `bmif_2_0`
# module, but our iso_c_bmi-based dynamic loader expects the matching
# `bmif_2_0_iso` shim. Patch the source in-place into the build tree.
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

miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_dynamic_fortran_heat_plugin
    SOURCES
        "${fortran_heat_model_root}/heat/heat.f90"
        "${fortran_heat_generated_dir}/bmi_heat.f90"
        register_bmi_heat.f90
    LINK_ISO_C_BMI
    FORTRAN_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mod"
)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_DYNAMIC_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

miaow_attach_plugin_paths(${EXECUTABLE_TARGET}
    MIAOW_CPP_HEAT_PLUGIN_PATH      miaow_dynamic_cpp_heat_plugin
    MIAOW_FORTRAN_HEAT_PLUGIN_PATH  miaow_dynamic_fortran_heat_plugin
)

# Drop the binary next to the source so example data (configs, realization
# files) can be referenced relative to the executable.
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_03_dynamic_bmi_extern_symlink_cpp
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${CMAKE_CURRENT_LIST_DIR}/extern/bmi-example-cxx
            -DEXPECT_TARGET=${cxx_heat_model_root}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )
    add_test(
        NAME miaow_example_03_dynamic_bmi_extern_symlink_fortran
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${CMAKE_CURRENT_LIST_DIR}/extern/bmi-example-fortran
            -DEXPECT_TARGET=${fortran_heat_model_root}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    string(CONCAT dynamic_bmi_pass_regex
        "C\\+\\+ dynamic BMI plugin: .*miaow_dynamic_cpp_heat_plugin(.|\\n)*"
        "Fortran dynamic BMI plugin: .*miaow_dynamic_fortran_heat_plugin(.|\\n)*"
        "\\[0\\] succeeded: BMI workflow complete(.|\\n)*"
        "\\[1\\] succeeded: BMI workflow complete"
    )

    add_test(
        NAME miaow_example_03_dynamic_bmi
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
            --max_workers
            2
            --task_request_timeout
            60
            --job_request_timeout
            600
    )
    set_tests_properties(miaow_example_03_dynamic_bmi PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "${dynamic_bmi_pass_regex}"
    )
endif()
