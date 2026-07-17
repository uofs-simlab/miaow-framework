cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_summa_example LANGUAGES CXX Fortran)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(summa)

# ExternalProject keeps SUMMA in its own configure and build process
include(ExternalProject)

# Allow a local checkout to override the pinned SUMMA source
set(MIAOW_SUMMA_MODEL_ROOT "" CACHE PATH
    "Path to a local SUMMA source tree")
set(summa_git_repository "https://github.com/tristanmontoya/summa.git")
# Pin the external build to the validated SUMMA revision
set(summa_git_tag "092013ac926924b109f32664ae7ba37b685cec49")

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
    MODEL_LOCAL_SUBDIR "summa"
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
        SOURCE_DIR "${summa_source_dir}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/summa-source-build"
    )
endif()

# Validate the files required to configure SUMMA and build its BMI library
if(NOT EXISTS "${summa_source_dir}/build/source/driver/summa_bmi.f90")
    if(MIAOW_SUMMA_MODEL_ROOT)
        message(FATAL_ERROR
            "MIAOW_SUMMA_MODEL_ROOT must point to a tristanmontoya/summa source tree")
    else()
        message(FATAL_ERROR
            "Fetched tristanmontoya/summa source tree is missing "
            "build/source/driver/summa_bmi.f90")
    endif()
endif()
if(NOT EXISTS "${summa_source_dir}/build/CMakeLists.txt")
    message(FATAL_ERROR
        "SUMMA source tree is missing build/CMakeLists.txt")
endif()

# Pass the same procedural BMI bridge source used by the parent MIAOW build
miaow_resolve_model_root(summa_iso_c_fortran_bmi_source_dir
    MODEL_ROOT "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}"
    MODEL_LOCAL_SUBDIR "iso_c_fortran_bmi"
    MODEL_LABEL "iso_c_fortran_bmi"
)
if(NOT EXISTS "${summa_iso_c_fortran_bmi_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "SUMMA requires iso_c_fortran_bmi source at "
        "${summa_iso_c_fortran_bmi_source_dir}")
endif()

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
set(summa_build_dir "${CMAKE_CURRENT_BINARY_DIR}/summa-build")
set(summa_bmi_library_filename
    "${CMAKE_SHARED_LIBRARY_PREFIX}summabmi${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(summa_bmi_library "${summa_build_dir}/${summa_bmi_library_filename}")

# Stage the built library where realization_config.json expects to load it
set(summa_realization_plugin_dir "${CMAKE_CURRENT_LIST_DIR}/data/plugins")
set(summa_realization_plugin_link
    "${summa_realization_plugin_dir}/libsummabmi")

# Configure and build only the SUMMA BMI shared library
ExternalProject_Add(miaow_summa_external
    SOURCE_DIR "${summa_source_dir}/build"
    BINARY_DIR "${summa_build_dir}"
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env ${summa_configure_environment}
        ${CMAKE_COMMAND}
        -S "${summa_source_dir}/build"
        -B "${summa_build_dir}"
        -DUSE_NEXTGEN=ON
        "-DSUMMA_ROOT=${summa_source_dir}"
        "-DISO_C_FORTRAN_BMI_ROOT=${summa_iso_c_fortran_bmi_source_dir}"
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

# Refresh the realization plugin link after each external build
ExternalProject_Add_Step(miaow_summa_external stage_plugin
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${summa_realization_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E remove -f
            "${summa_realization_plugin_link}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${summa_bmi_library}"
            "${summa_realization_plugin_link}"
    DEPENDEES build
    DEPENDERS install
    BYPRODUCTS "${summa_realization_plugin_link}"
    ALWAYS TRUE
)

# Build the driver after the realization plugin link is ready
add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_SUMMA_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)
add_dependencies(${EXECUTABLE_TARGET} miaow_subprocess miaow_summa_external)

# Place the binary beside the source so relative example paths remain stable
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    # Confirm the runtime-critical realization plugin link targets this build
    add_test(
        NAME miaow_example_04_summa_plugin_symlink
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${summa_realization_plugin_link}
            -DEXPECT_TARGET=${summa_bmi_library}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    # Describe the metadata and result files required from a successful run
    set(summa_expected_job_record_snippets
        "\"feature_id\""
        "\"hydrofabric_layer\""
        "\"hydrofabric_id_field\""
        "\"cat-11223.json\""
        "\"results/cat-11223.csv\""
    )
    list(JOIN summa_expected_job_record_snippets ";"
        summa_job_record_snippets_arg)
    set(summa_expected_result_paths
        "cat-11223.json"
        "cat-11224.json"
        "cat-11371.json"
        "cat-11410.json"
        "cat-11509.json"
        "results/cat-11223.csv"
        "results/cat-11224.csv"
        "results/cat-11371.csv"
        "results/cat-11410.csv"
        "results/cat-11509.csv"
    )
    list(JOIN summa_expected_result_paths ";"
        summa_result_paths_arg)

    # Run SUMMA once and validate its complete job-record layout
    add_test(
        NAME miaow_example_04_summa_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output/gauge_01073000
            -DREQUIRE_SUCCESS=TRUE
            "-DEXPECTED_METADATA_SNIPPETS=${summa_job_record_snippets_arg}"
            "-DEXPECTED_RESULT_PATHS=${summa_result_paths_arg}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_04_summa_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_04_summa_output
    )

    # Confirm the example rejects a working directory with unresolved data paths
    add_test(
        NAME miaow_example_04_summa_wrong_cwd
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_04_summa_wrong_cwd PROPERTIES
        WORKING_DIRECTORY "${MIAOW_ROOT}"
        PASS_REGULAR_EXPRESSION "Run directory error: expected"
    )
endif()
