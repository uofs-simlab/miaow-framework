cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_fortran_heat_example CXX Fortran)
endif()

if(NOT DEFINED MIAOW_ROOT)
    get_filename_component(miaow_root_default "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(MIAOW_ROOT "${miaow_root_default}" CACHE PATH
        "Path to the miaow-framework source root")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EXECUTABLE_TARGET "miaow_fortran_heat" CACHE STRING
        "Executable target name for this example")
else()
    set(EXECUTABLE_TARGET "miaow_fortran_heat")
endif()

if(NOT TARGET miaow)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
    add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
endif()

set(MIAOW_FORTRAN_HEAT_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-fortran checkout")
set(MIAOW_FORTRAN_HEAT_MODEL_GIT_TAG
    "74df8d888433c43fc7f7b96109a445bead9972d2"
    CACHE STRING
    "csdms/bmi-example-fortran commit used for the Fortran heat example")

if(MIAOW_FORTRAN_HEAT_MODEL_ROOT)
    set(fortran_heat_model_root "${MIAOW_FORTRAN_HEAT_MODEL_ROOT}")
else()
    set(fortran_heat_model_root "${MIAOW_ROOT}/third_party/bmi-example-fortran")
endif()

if(NOT EXISTS "${fortran_heat_model_root}")
    include(FetchContent)
    FetchContent_Declare(
        bmi_example_fortran
        GIT_REPOSITORY https://github.com/csdms/bmi-example-fortran.git
        GIT_TAG "${MIAOW_FORTRAN_HEAT_MODEL_GIT_TAG}"
        SOURCE_DIR "${fortran_heat_model_root}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/bmi_example_fortran-build"
    )

    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_GetProperties(bmi_example_fortran)
    if(NOT bmi_example_fortran_POPULATED)
        FetchContent_Populate(bmi_example_fortran)
    endif()
endif()

if(NOT EXISTS "${fortran_heat_model_root}/heat/heat.f90")
    message(FATAL_ERROR
        "MIAOW_FORTRAN_HEAT_MODEL_ROOT must point to a csdms/bmi-example-fortran checkout")
endif()

set(fortran_heat_module_dir "${CMAKE_CURRENT_BINARY_DIR}/mod")
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

add_library(miaow_fortran_heat_plugin SHARED
    "${fortran_heat_model_root}/heat/heat.f90"
    "${fortran_heat_generated_dir}/bmi_heat.f90"
    register_bmi_heat.f90
)

target_link_libraries(miaow_fortran_heat_plugin
    PRIVATE
        iso_c_bmi
)

set_target_properties(miaow_fortran_heat_plugin
    PROPERTIES
        Fortran_MODULE_DIRECTORY "${fortran_heat_module_dir}"
)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_FORTRAN_HEAT_PLUGIN_PATH="$<TARGET_FILE:miaow_fortran_heat_plugin>"
    MIAOW_FORTRAN_HEAT_CONFIG_PATH="${fortran_heat_model_root}/example/test1.cfg"
)
add_dependencies(${EXECUTABLE_TARGET} miaow_fortran_heat_plugin)

set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_03_fortran_heat
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
            --output_root
            "${CMAKE_CURRENT_BINARY_DIR}/output_smoke"
            --max_workers
            2
            --task_request_timeout
            60
            --job_request_timeout
            600
    )
    set_tests_properties(miaow_example_03_fortran_heat PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "\\[0\\] succeeded: BMI workflow complete(.|\\n)*\\[1\\] succeeded: BMI workflow complete"
    )

    add_test(
        NAME miaow_example_03_fortran_heat_invalid_plugin
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
            --expect_failure
            --plugin_path
            "${CMAKE_CURRENT_BINARY_DIR}/missing_fortran_heat_plugin"
    )
    set_tests_properties(miaow_example_03_fortran_heat_invalid_plugin PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PASS_REGULAR_EXPRESSION "BMI error: unable to load BMI plugin"
    )
endif()
