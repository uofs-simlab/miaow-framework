cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(summa Fortran)

include(ExternalProject)

set(MIAOW_SUMMA_MODEL_ROOT "" CACHE PATH
    "Path to a local SUMMA source tree")
set(summa_git_repository "https://github.com/tristanmontoya/summa.git")
set(summa_git_tag "develop")

if(APPLE)
    set(miaow_summa_library_links_default "-framework Accelerate")
else()
    set(miaow_summa_library_links_default "")
endif()
set(MIAOW_SUMMA_LIBRARY_LINKS "${miaow_summa_library_links_default}" CACHE STRING
    "LAPACK/BLAS link flags passed to SUMMA through LIBRARY_LINKS")

if(MIAOW_SUMMA_MODEL_ROOT)
    set(summa_model_root_override "${MIAOW_SUMMA_MODEL_ROOT}")
else()
    set(summa_model_root_override "")
endif()
miaow_resolve_model_root(summa_source_dir
    MODEL_ROOT "${summa_model_root_override}"
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

if(NOT EXISTS "${summa_source_dir}/build/source/driver/summa_bmi.f90")
    if(summa_model_root_override)
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

miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "summa"
    TARGET_DIR "${summa_source_dir}"
)

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

set(summa_configure_environment)
set(summa_lapack_args)
if(NOT MIAOW_SUMMA_LIBRARY_LINKS STREQUAL "")
    list(APPEND summa_configure_environment
        "LIBRARY_LINKS=${MIAOW_SUMMA_LIBRARY_LINKS}")
    list(APPEND summa_lapack_args
        -DSPECIFY_LAPACK_LINKS=ON)
endif()

# The parent project may not enable every language that SUMMA enables
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

set(summa_build_dir "${CMAKE_CURRENT_BINARY_DIR}/summa-build")
string(CONCAT summa_bmi_library
    "${summa_build_dir}/"
    "${CMAKE_SHARED_LIBRARY_PREFIX}summabmi${CMAKE_SHARED_LIBRARY_SUFFIX}")

# Build SUMMA with its own CMake target instead of duplicating the large
# Fortran source graph in this example
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

# Keep realization library paths stable across build directories
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
            "${summa_bmi_library}"
            "${summa_realization_plugin_link}"
    DEPENDS miaow_summa_external "${summa_bmi_library}"
    VERBATIM
)
add_custom_target(miaow_summa_plugin_link
    DEPENDS "${summa_realization_plugin_link}"
)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_include_directories(${EXECUTABLE_TARGET} PRIVATE
    "${MIAOW_ROOT}/src"
)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_SUMMA_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)
add_dependencies(${EXECUTABLE_TARGET} miaow_summa_plugin_link)

# Drop the binary next to the source so example data can be referenced locally
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_test(
        NAME miaow_example_04_summa_extern_symlink
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${CMAKE_CURRENT_LIST_DIR}/extern/summa
            -DEXPECT_TARGET=${summa_source_dir}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    add_test(
        NAME miaow_example_04_summa_plugin_symlink
        COMMAND
            ${CMAKE_COMMAND}
            -DLINK_PATH=${summa_realization_plugin_link}
            -DEXPECT_TARGET=${summa_bmi_library}
            -P
            ${MIAOW_ROOT}/cmake/check_symlink.cmake
    )

    add_test(
        NAME miaow_example_04_summa_run
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
            --job_request_timeout
            900
    )
    string(CONCAT summa_run_regex
        "Parsed realization with 5 feature pipeline config\\(s\\)(.|\\n)*"
        "\\[0\\] succeeded: BMI workflow complete"
    )
    set_tests_properties(miaow_example_04_summa_run PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        PASS_REGULAR_EXPRESSION "${summa_run_regex}"
        RESOURCE_LOCK miaow_example_04_summa_output
    )

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

    add_test(
        NAME miaow_example_04_summa_job_record_layout
        COMMAND
            ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${EXECUTABLE_TARGET}>
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_ROOT=${CMAKE_CURRENT_LIST_DIR}/output
            "-DEXPECTED_METADATA_SNIPPETS=${summa_job_record_snippets_arg}"
            "-DEXPECTED_RESULT_PATHS=${summa_result_paths_arg}"
            -P
            ${MIAOW_ROOT}/cmake/check_job_record_layout.cmake
    )
    set_tests_properties(miaow_example_04_summa_job_record_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESOURCE_LOCK miaow_example_04_summa_output
    )

    add_test(
        NAME miaow_example_04_summa_wrong_cwd
        COMMAND
            $<TARGET_FILE:${EXECUTABLE_TARGET}>
    )
    set_tests_properties(miaow_example_04_summa_wrong_cwd PROPERTIES
        WORKING_DIRECTORY "${MIAOW_ROOT}"
        PASS_REGULAR_EXPRESSION "Run directory error: miaow_summa must be run from"
    )
endif()
