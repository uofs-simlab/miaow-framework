# ExternalProject keeps SUMMA in its own configure and build process
include(ExternalProject)

# Allow a local checkout to override the pinned SUMMA source
set(MIAOW_SUMMA_MODEL_ROOT "" CACHE PATH
    "Path to a local SUMMA source tree")
set(summa_git_repository "https://github.com/CH-Earth/summa.git")
# Resolve the release to an immutable revision for reproducible model builds
set(summa_release "v4.5.0-exp")
set(summa_git_tag "5a5f12ed8331deeda3729598ffe390dd068e3685")

# Supply the platform BLAS and LAPACK link flags expected by SUMMA
if(APPLE)
    set(miaow_summa_library_links_default "-framework Accelerate")
else()
    set(miaow_summa_library_links_default "")
endif()
set(MIAOW_SUMMA_LIBRARY_LINKS "${miaow_summa_library_links_default}" CACHE STRING
    "LAPACK/BLAS link flags passed to SUMMA through LIBRARY_LINKS")

# Choose the SUMMA path, fetching the pinned source when it is absent
miaow_resolve_model_root(summa_source_dir
    MODEL_ROOT "${MIAOW_SUMMA_MODEL_ROOT}"
    MODEL_LOCAL_SUBDIR "summa-${summa_release}"
    MODEL_LABEL "summa"
)

if(NOT EXISTS "${summa_source_dir}")
    get_filename_component(summa_source_parent "${summa_source_dir}" DIRECTORY)
    file(MAKE_DIRECTORY "${summa_source_parent}")

    include(FetchContent)
    FetchContent_Populate(
        summa_source
        GIT_REPOSITORY "${summa_git_repository}"
        GIT_TAG "${summa_git_tag}"
        GIT_SUBMODULES ""
        SOURCE_DIR "${summa_source_dir}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/summa-source-build"
    )
endif()

# Validate the files required to configure SUMMA and build its BMI library
if(NOT EXISTS "${summa_source_dir}/build/source/driver/summa_bmi.f90")
    if(MIAOW_SUMMA_MODEL_ROOT)
        message(FATAL_ERROR
            "MIAOW_SUMMA_MODEL_ROOT must point to a SUMMA source tree with BMI support")
    else()
        message(FATAL_ERROR
            "Fetched CH-Earth/summa source tree is missing "
            "build/source/driver/summa_bmi.f90")
    endif()
endif()
if(NOT EXISTS "${summa_source_dir}/build/CMakeLists.txt")
    message(FATAL_ERROR
        "SUMMA source tree is missing build/CMakeLists.txt")
endif()

# Pass the same procedural BMI bridge source used by the parent MIAOW build
if(MIAOW_ISO_C_FORTRAN_BMI_ROOT)
    set(summa_iso_c_fortran_bmi_source_dir "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}")
else()
    set(summa_iso_c_fortran_bmi_source_dir
        "${MIAOW_DEFAULT_MODEL_BASE_DIR}/iso_c_fortran_bmi")
endif()
if(NOT EXISTS "${summa_iso_c_fortran_bmi_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "SUMMA requires iso_c_fortran_bmi source at "
        "${summa_iso_c_fortran_bmi_source_dir}")
endif()

# Upstream's BMI build uses the NextGen layout: a driver CMakeLists.txt with a
# summa checkout below it and the procedural BMI bridge in a sibling directory.
# Stage that layout without modifying either dependency's build files.
set(summa_project_dir "${CMAKE_CURRENT_BINARY_DIR}/summa-project")
set(summa_driver_dir "${summa_project_dir}/driver")
file(MAKE_DIRECTORY "${summa_driver_dir}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${summa_source_dir}/build/CMakeLists.txt")
file(READ "${summa_source_dir}/build/CMakeLists.txt" summa_driver_cmake)
# SUMMA resolves the checkout symlink, so its source directory needs an explicit
# binary directory when used from the staged driver.
string(REPLACE "add_subdirectory(\${F_MASTER}/build/source/)"
    "add_subdirectory(\${F_MASTER}/build/source/ \${CMAKE_BINARY_DIR}/source)"
    summa_driver_cmake "${summa_driver_cmake}")
file(CONFIGURE OUTPUT "${summa_driver_dir}/CMakeLists.txt"
    CONTENT "${summa_driver_cmake}" @ONLY)
configure_file("${summa_source_dir}/build/cmake/summabmi.pc.in"
    "${summa_driver_dir}/summabmi.pc.in" COPYONLY)
file(CREATE_LINK "${summa_source_dir}" "${summa_driver_dir}/summa" SYMBOLIC)
file(CREATE_LINK "${summa_iso_c_fortran_bmi_source_dir}"
    "${summa_project_dir}/iso_c_fortran_bmi" SYMBOLIC)

# Translate the selected library flags into SUMMA configure arguments
set(summa_configure_environment)
set(summa_lapack_args)
if(NOT MIAOW_SUMMA_LIBRARY_LINKS STREQUAL "")
    list(APPEND summa_configure_environment
        "LIBRARY_LINKS=${MIAOW_SUMMA_LIBRARY_LINKS}")
    list(APPEND summa_lapack_args
        -DSPECIFY_LAPACK_LINKS=ON)
endif()

# Forward compilers so the separate SUMMA build uses the same toolchain
set(summa_compiler_args)
if(CMAKE_Fortran_COMPILER)
    list(APPEND summa_configure_environment
        "FC=${CMAKE_Fortran_COMPILER}")
    list(APPEND summa_compiler_args
        -DCMAKE_Fortran_COMPILER=${CMAKE_Fortran_COMPILER})
endif()
if(CMAKE_C_COMPILER)
    list(APPEND summa_configure_environment
        "CC=${CMAKE_C_COMPILER}")
    list(APPEND summa_compiler_args
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
else()
    find_program(summa_c_compiler NAMES cc clang gcc)
    if(summa_c_compiler)
        list(APPEND summa_configure_environment
            "CC=${summa_c_compiler}")
        list(APPEND summa_compiler_args
            -DCMAKE_C_COMPILER=${summa_c_compiler})
    endif()
endif()
if(CMAKE_CXX_COMPILER)
    list(APPEND summa_configure_environment
        "CXX=${CMAKE_CXX_COMPILER}")
    list(APPEND summa_compiler_args
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()

# Predict the shared library path produced by the external build
set(summa_build_dir "${CMAKE_CURRENT_BINARY_DIR}/summa-build-${summa_release}")
string(CONCAT summa_bmi_library
    "${summa_build_dir}/"
    "${CMAKE_SHARED_LIBRARY_PREFIX}summabmi${CMAKE_SHARED_LIBRARY_SUFFIX}")

# Configure and build only the SUMMA BMI shared library
ExternalProject_Add(miaow_summa_external
    SOURCE_DIR "${summa_driver_dir}"
    BINARY_DIR "${summa_build_dir}"
    # Upstream records its Git version from the configure process's working directory
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E chdir "${summa_source_dir}"
        ${CMAKE_COMMAND} -E env ${summa_configure_environment}
        ${CMAKE_COMMAND}
        -S "${summa_driver_dir}"
        -B "${summa_build_dir}"
        -DUSE_NEXTGEN=ON
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        ${summa_compiler_args}
        ${summa_lapack_args}
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env ${summa_configure_environment}
        ${CMAKE_COMMAND}
        --build "${summa_build_dir}"
        --target summabmi
        --config $<CONFIG>
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${summa_bmi_library}"
)

# Export a factory even when SUMMA's module-private entry point is hidden
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")
miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_summa_bmi
    SOURCES register_bmi_summa.f90
    INCLUDE_DIRS
        "${summa_build_dir}/fortran"
        "${summa_build_dir}/summa_nrtype_mod"
        "${summa_build_dir}/summa_data_types_mod"
        "${summa_build_dir}/summa_types_mod"
        "${summa_build_dir}/summa_comm_mod"
    LINK_ISO_C_BMI
    FORTRAN_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugin-mod"
)
target_link_libraries(miaow_summa_bmi PRIVATE "${summa_bmi_library}")
add_dependencies(miaow_summa_bmi miaow_summa_external)

# Link the plugin where realization_config.json expects to load it
set(summa_realization_plugin_dir "${CMAKE_CURRENT_LIST_DIR}/data/plugins")
set(summa_realization_plugin_link
    "${summa_realization_plugin_dir}/libsummabmi")
add_custom_command(
    OUTPUT "${summa_realization_plugin_link}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${summa_realization_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E remove -f
            "${summa_realization_plugin_link}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "$<TARGET_FILE:miaow_summa_bmi>"
            "${summa_realization_plugin_link}"
    DEPENDS miaow_summa_bmi
    VERBATIM
)
add_custom_target(miaow_summa_plugin_link
    DEPENDS "${summa_realization_plugin_link}"
)

# Build the driver after the realization plugin link is ready
add_executable(miaow_summa
    main.cpp
    forcing_comparison.cpp
)

target_compile_features(miaow_summa PRIVATE cxx_std_20)
target_link_libraries(miaow_summa PRIVATE miaow nlohmann_json::nlohmann_json)
target_compile_definitions(miaow_summa PRIVATE
    MIAOW_SUMMA_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)
add_dependencies(miaow_summa miaow_summa_plugin_link miaow_subprocess)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_summa PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_04_summa
        EXECUTABLE_TARGET miaow_summa
        EXTERNAL_MODEL
    )
    # SUMMA's native output files are shared by all executions in this directory
    set_tests_properties(miaow_example_04_summa PROPERTIES
        RESOURCE_LOCK miaow_summa_output)
    foreach(workers IN ITEMS 1 5)
        set(comparison_test "miaow_example_04_summa_forcing_${workers}_workers")
        add_test(NAME ${comparison_test}
            COMMAND $<TARGET_FILE:miaow_summa> --compare_forcing
                --max_workers ${workers}
                --output_root "output/forcing_comparison_${workers}_workers")
        set_tests_properties(${comparison_test} PROPERTIES
            LABELS "example;external_model"
            TIMEOUT 720
            RESOURCE_LOCK miaow_summa_output
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
endif()
