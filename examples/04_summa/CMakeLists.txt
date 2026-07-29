# ExternalProject keeps SUMMA in its own configure and build process
include(ExternalProject)

# Allow a local checkout to override the pinned SUMMA source
set(MIAOW_SUMMA_MODEL_ROOT "" CACHE PATH
    "Path to a local SUMMA source tree")
set(summa_git_repository "https://github.com/tristanmontoya/summa.git")
# Pin the external build to the validated SUMMA revision
set(summa_git_tag "f719ded11a4fb7be3abf05b85332f20744ae4f2c")

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
string(CONCAT summa_bmi_library
    "${summa_build_dir}/"
    "${CMAKE_SHARED_LIBRARY_PREFIX}summabmi${CMAKE_SHARED_LIBRARY_SUFFIX}")

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

# Link the built library where realization_config.json expects to load it
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

# Build the driver after the realization plugin link is ready
add_executable(miaow_summa
    main.cpp
)

target_compile_features(miaow_summa PRIVATE cxx_std_20)
target_link_libraries(miaow_summa PRIVATE miaow)
target_compile_definitions(miaow_summa PRIVATE
    MIAOW_SUMMA_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)
add_dependencies(miaow_summa miaow_summa_plugin_link)

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
endif()
