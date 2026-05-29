cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(noah_owp Fortran)

include("${MIAOW_ROOT}/cmake/miaow_dynamic_bmi_plugin.cmake")

set(MIAOW_NOAH_OWP_MODEL_ROOT "" CACHE PATH
    "Path to a local NOAA-OWP/noah-owp-modular source tree")
# Resolve/fetch the NOAA-OWP/noah-owp-modular reference model
set(NOAH_OWP_GIT_TAG "0ff055efd93c37f5bce83255f06180e2e779cea6" CACHE STRING
    "NOAA-OWP/noah-owp-modular Git tag, branch, or commit to fetch")

if(MIAOW_NOAH_OWP_MODEL_ROOT)
    set(noah_owp_model_root_override "${MIAOW_NOAH_OWP_MODEL_ROOT}")
else()
    set(noah_owp_model_root_override "")
endif()
miaow_resolve_model_root(noah_owp_source_dir
    MODEL_ROOT "${noah_owp_model_root_override}"
    MODEL_LOCAL_SUBDIR "noah-owp-modular"
    MODEL_LABEL "noah_owp"
)

if(NOT EXISTS "${noah_owp_source_dir}")
    get_filename_component(noah_owp_source_parent "${noah_owp_source_dir}" DIRECTORY)
    file(MAKE_DIRECTORY "${noah_owp_source_parent}")

    include(FetchContent)
    FetchContent_Populate(
        noah_owp_modular_source
        GIT_REPOSITORY "https://github.com/NOAA-OWP/noah-owp-modular.git"
        GIT_TAG "${NOAH_OWP_GIT_TAG}"
        SOURCE_DIR "${noah_owp_source_dir}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/noah-owp-modular-source-build"
    )
endif()

if(NOT EXISTS "${noah_owp_source_dir}/bmi/bmi_noahowp.f90")
    if(noah_owp_model_root_override)
        message(FATAL_ERROR
            "MIAOW_NOAH_OWP_MODEL_ROOT must point to a "
            "NOAA-OWP/noah-owp-modular source tree")
    else()
        message(FATAL_ERROR
            "Fetched NOAA-OWP/noah-owp-modular source tree is missing bmi/bmi_noahowp.f90")
    endif()
endif()

miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "noah-owp-modular"
    TARGET_DIR "${noah_owp_source_dir}"
)

# Discover NetCDF Fortran. Noah-OWP-Modular reads forcing/parameter NetCDF
# files at runtime, so the plugin must link against both libnetcdff and
# libnetcdf and find the matching netcdf.mod for Fortran compilation.
set(NOAH_OWP_NETCDF_ROOT "" CACHE PATH
    "Path to a NetCDF installation for Noah-OWP-Modular")
find_path(NOAH_OWP_NETCDF_INCLUDE_DIR
    NAMES netcdf.mod
    HINTS
        "${NOAH_OWP_NETCDF_ROOT}"
        "$ENV{NETCDF}"
    PATH_SUFFIXES include
)
find_library(NOAH_OWP_NETCDF_C_LIBRARY
    NAMES netcdf
    HINTS
        "${NOAH_OWP_NETCDF_ROOT}"
        "$ENV{NETCDF}"
    PATH_SUFFIXES lib
)
find_library(NOAH_OWP_NETCDF_FORTRAN_LIBRARY
    NAMES netcdff
    HINTS
        "${NOAH_OWP_NETCDF_ROOT}"
        "$ENV{NETCDF}"
    PATH_SUFFIXES lib
)

if(NOT NOAH_OWP_NETCDF_INCLUDE_DIR OR
        NOT NOAH_OWP_NETCDF_C_LIBRARY OR
        NOT NOAH_OWP_NETCDF_FORTRAN_LIBRARY)
    message(FATAL_ERROR
        "Noah-OWP-Modular requires NetCDF Fortran. Set "
        "NOAH_OWP_NETCDF_ROOT, or set NOAH_OWP_NETCDF_INCLUDE_DIR, "
        "NOAH_OWP_NETCDF_C_LIBRARY, and NOAH_OWP_NETCDF_FORTRAN_LIBRARY")
endif()

# Fortran modules must be compiled before the modules that `use` them. 
# The order below is the topological order published by the upstream 
# NOAA-OWP/noah-owp-modular CMake build.
set(noah_owp_plugin_sources
    "${noah_owp_source_dir}/src/ErrorCheckModule.f90"
    "${noah_owp_source_dir}/src/NamelistRead.f90"
    "${noah_owp_source_dir}/src/ParametersRead.f90"
    "${noah_owp_source_dir}/src/LevelsType.f90"
    "${noah_owp_source_dir}/src/DomainType.f90"
    "${noah_owp_source_dir}/src/OptionsType.f90"
    "${noah_owp_source_dir}/src/ParametersType.f90"
    "${noah_owp_source_dir}/src/ForcingType.f90"
    "${noah_owp_source_dir}/src/EnergyType.f90"
    "${noah_owp_source_dir}/src/WaterType.f90"
    "${noah_owp_source_dir}/src/WaterModule.f90"
    "${noah_owp_source_dir}/src/CanopyWaterModule.f90"
    "${noah_owp_source_dir}/src/SnowWaterModule.f90"
    "${noah_owp_source_dir}/src/SnowWaterRenew.f90"
    "${noah_owp_source_dir}/src/SnowLayerChange.f90"
    "${noah_owp_source_dir}/src/SoilWaterModule.f90"
    "${noah_owp_source_dir}/src/SoilWaterRetentionCoeff.f90"
    "${noah_owp_source_dir}/src/SurfaceRunoffInfiltration.f90"
    "${noah_owp_source_dir}/src/SurfaceRunoffModule.f90"
    "${noah_owp_source_dir}/src/SoilWaterMovement.f90"
    "${noah_owp_source_dir}/src/SubsurfaceRunoffModule.f90"
    "${noah_owp_source_dir}/src/AtmProcessing.f90"
    "${noah_owp_source_dir}/src/ForcingModule.f90"
    "${noah_owp_source_dir}/src/InterceptionModule.f90"
    "${noah_owp_source_dir}/src/ThermalPropertiesModule.f90"
    "${noah_owp_source_dir}/src/AlbedoModule.f90"
    "${noah_owp_source_dir}/src/ShortwaveRadiationModule.f90"
    "${noah_owp_source_dir}/src/PrecipHeatModule.f90"
    "${noah_owp_source_dir}/src/EtFluxModule.f90"
    "${noah_owp_source_dir}/src/SnowSoilTempModule.f90"
    "${noah_owp_source_dir}/src/EnergyModule.f90"
    "${noah_owp_source_dir}/src/UtilitiesModule.f90"
    "${noah_owp_source_dir}/src/DateTimeUtilsModule.f90"
    "${noah_owp_source_dir}/src/RunModule.f90"
    "${noah_owp_source_dir}/driver/OutputModule.f90"
    "${noah_owp_source_dir}/driver/AsciiReadModule.f90"
    "${noah_owp_source_dir}/bmi/bmi_noahowp.f90"
)

# OUTPUT_NAME=surfacebmi matches the filename Noah-OWP's BMI registration
# code expects when the plugin is loaded via dlopen. The NGEN_* preprocessor
# flags select NextGen-compatible code paths in Noah-OWP:
#   NGEN_ACTIVE         - enables BMI-side hooks in bmi_noahowp.f90 (required)
#   NGEN_OUTPUT_ACTIVE  - compiles out the internal NetCDF output writer so
#                         MIAOW's BMI GetValue pipeline is the sole output path
#   NGEN_FORCING_ACTIVE - drives forcing through BMI SetValue
miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_noah_owp_plugin
    SOURCES ${noah_owp_plugin_sources}
    INCLUDE_DIRS
        "${noah_owp_source_dir}/src"
        "${noah_owp_source_dir}/driver"
        "${noah_owp_source_dir}/bmi"
        "${NOAH_OWP_NETCDF_INCLUDE_DIR}"
    COMPILE_DEFINITIONS NGEN_ACTIVE NGEN_OUTPUT_ACTIVE NGEN_FORCING_ACTIVE
    COMPILE_OPTIONS
        $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-cpp>
        $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-ffree-form>
        $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-ffree-line-length-none>
        $<$<COMPILE_LANG_AND_ID:Fortran,Intel,IntelLLVM>:-fpp>
        $<$<COMPILE_LANG_AND_ID:Fortran,Intel,IntelLLVM>:-free>
    LINK_LIBRARIES
        "${NOAH_OWP_NETCDF_FORTRAN_LIBRARY}"
        "${NOAH_OWP_NETCDF_C_LIBRARY}"
    LINK_ISO_C_BMI
    FORTRAN_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/noah-owp-modular-mod"
    OUTPUT_NAME surfacebmi
)

# Keep realization library paths stable across build directories
set(noah_owp_realization_plugin_dir "${CMAKE_CURRENT_LIST_DIR}/data/plugins")
set(noah_owp_realization_plugin_link
    "${noah_owp_realization_plugin_dir}/libsurfacebmi")
add_custom_command(TARGET miaow_noah_owp_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${noah_owp_realization_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E remove -f
            "${noah_owp_realization_plugin_link}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "$<TARGET_FILE:miaow_noah_owp_plugin>"
            "${noah_owp_realization_plugin_link}"
)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_17)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_include_directories(${EXECUTABLE_TARGET} PRIVATE
    "${MIAOW_ROOT}/src"
)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_NOAH_OWP_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

miaow_attach_plugin_paths(${EXECUTABLE_TARGET}
    MIAOW_NOAH_OWP_PLUGIN_PATH miaow_noah_owp_plugin
)

# Drop the binary next to the source so example data (configs, realization
# files) can be referenced relative to the executable.
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_04_noah_owp_extern_symlink
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${CMAKE_CURRENT_LIST_DIR}/extern/noah-owp-modular
            -DEXPECT_TARGET=${noah_owp_source_dir}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    add_test(
        NAME miaow_example_04_noah_owp_run
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    string(CONCAT noah_owp_run_regex
        "Parsed realization with 3 feature pipeline config\\(s\\)(.|\\n)*"
        "\\[0\\] succeeded: BMI workflow complete"
    )
    set_tests_properties(miaow_example_04_noah_owp_run PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "${noah_owp_run_regex}"
    )

    set(noah_owp_expected_job_record_snippets
        "\"feature_id\""
        "\"hydrofabric_layer\""
        "\"hydrofabric_id_field\""
        "\"cat-552.json\""
        "\"results/cat-552.csv\""
    )
    list(JOIN noah_owp_expected_job_record_snippets ";"
        noah_owp_job_record_snippets_arg)
    set(noah_owp_expected_result_paths
        "bmi_subprocess_cat-552_input.json"
        "bmi_subprocess_cat-552_result.json"
        "bmi_subprocess_cat-552_stdout.log"
        "bmi_subprocess_cat-552_stderr.log"
        "bmi_subprocess_cat-562_input.json"
        "bmi_subprocess_cat-562_result.json"
        "bmi_subprocess_cat-562_stdout.log"
        "bmi_subprocess_cat-562_stderr.log"
        "bmi_subprocess_cat-585_input.json"
        "bmi_subprocess_cat-585_result.json"
        "bmi_subprocess_cat-585_stdout.log"
        "bmi_subprocess_cat-585_stderr.log"
        "cat-552.json"
        "cat-562.json"
        "cat-585.json"
        "results/cat-552.csv"
        "results/cat-562.csv"
        "results/cat-585.csv"
    )
    list(JOIN noah_owp_expected_result_paths ";"
        noah_owp_result_paths_arg)

    add_test(
        NAME miaow_example_04_noah_owp_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            "-DEXPECTED_METADATA_SNIPPETS=${noah_owp_job_record_snippets_arg}"
            "-DEXPECTED_RESULT_PATHS=${noah_owp_result_paths_arg}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_04_noah_owp_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_04_noah_owp_output
    )

    add_test(
        NAME miaow_example_04_noah_owp_wrong_cwd
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_04_noah_owp_wrong_cwd PROPERTIES
        WORKING_DIRECTORY "${MIAOW_ROOT}"
        PASS_REGULAR_EXPRESSION "Run directory error: miaow_noah_owp must be run from"
    )
endif()
