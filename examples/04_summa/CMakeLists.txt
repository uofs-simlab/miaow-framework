# ExternalProject keeps SUMMA in its own configure and build process
include(ExternalProject)

# Allow a local checkout to override the pinned SUMMA source
set(MIAOW_SUMMA_MODEL_ROOT "" CACHE PATH
    "Path to a local SUMMA source tree")
set(summa_git_repository "https://github.com/tristanmontoya/summa.git")
# Direct BMI builds with native forcing/output and a complete BMI clock
set(summa_git_tag "6932a264b23ea3b7783f5891b4245a3c6b4ecb93")
string(SUBSTRING "${summa_git_tag}" 0 7 summa_git_short_tag)

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
    MODEL_LOCAL_SUBDIR "summa-${summa_git_short_tag}"
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

# Verify the pinned model revision before configuring the BMI build
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${summa_source_dir}"
    OUTPUT_VARIABLE summa_actual_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT summa_actual_revision STREQUAL summa_git_tag)
    message(FATAL_ERROR
        "SUMMA source must be at ${summa_git_tag}, found ${summa_actual_revision}")
endif()

# Validate the files required to configure SUMMA and build its BMI library
if(NOT EXISTS "${summa_source_dir}/build/source/driver/summa_bmi.f90")
    if(MIAOW_SUMMA_MODEL_ROOT)
        message(FATAL_ERROR
            "MIAOW_SUMMA_MODEL_ROOT must point to a SUMMA source tree with BMI support")
    else()
        message(FATAL_ERROR
            "Fetched SUMMA source tree is missing "
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
set(summa_build_dir "${CMAKE_CURRENT_BINARY_DIR}/summa-build-${summa_git_short_tag}")
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
        -DUSE_NGEN_FORCING=OFF
        -DUSE_NGEN_OUTPUT=OFF
        -DSUMMA_ISO_C_FORTRAN_BMI_ROOT=${summa_iso_c_fortran_bmi_source_dir}
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
    LOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/logs"
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# Build serial routing with the upstream GNU Fortran Makefile
find_program(mizuroute_make NAMES gmake make REQUIRED)
find_program(mizuroute_fortran_compiler NAMES gfortran REQUIRED)
find_program(mizuroute_nf_config NAMES nf-config REQUIRED)
find_program(mizuroute_nc_config NAMES nc-config REQUIRED)
execute_process(
    COMMAND "${mizuroute_nf_config}" --prefix
    OUTPUT_VARIABLE mizuroute_netcdf_prefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${mizuroute_nf_config}" --flibs
    OUTPUT_VARIABLE mizuroute_netcdf_fortran_libraries
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${mizuroute_nc_config}" --libs
    OUTPUT_VARIABLE mizuroute_netcdf_c_libraries
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
set(mizuroute_revision "104ef346f14bb387f4c29dbb742dbfefbe2a78e6")
set(mizuroute_source_dir "${CMAKE_CURRENT_BINARY_DIR}/mizuRoute-${mizuroute_revision}")
ExternalProject_Add(mizuroute
    URL "https://codeload.github.com/ESCOMP/mizuRoute/tar.gz/${mizuroute_revision}"
    URL_HASH SHA256=bb5e94e397ae06e03b6d28462c9efdff00b734e7b4a1c1ea1db8fb95c31b8a37
    DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloads"
    DOWNLOAD_NAME mizuroute.tar.gz
    SOURCE_DIR "${mizuroute_source_dir}"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    # The upstream compile, install, and clean targets must run in sequence
    BUILD_COMMAND
        "${mizuroute_make}" -j1 -C <SOURCE_DIR>/route/build
        FC=gnu
        "FC_EXE=${mizuroute_fortran_compiler}"
        EXE=route_runoff
        "EXE_PATH=${CMAKE_CURRENT_BINARY_DIR}"
        MODE=fast
        "F_MASTER=<SOURCE_DIR>/route/"
        "NCDF_PATH=${mizuroute_netcdf_prefix}"
        isOpenMP=no
        "LIBNETCDF=${mizuroute_netcdf_fortran_libraries} ${mizuroute_netcdf_c_libraries}"
        VERSION=v1.2.3
        "GITHASH=${mizuroute_revision}"
        "GITBRCH=${mizuroute_revision}"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/route_runoff"
    EXCLUDE_FROM_ALL TRUE
    LOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/logs"
    LOG_BUILD TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# SUMMA exports the C registration factory for direct BMI loading
add_library(summabmi SHARED IMPORTED)
set_target_properties(summabmi PROPERTIES IMPORTED_LOCATION "${summa_bmi_library}")
add_dependencies(summabmi miaow_summa_external)

# Build the driver after the external model library is ready
add_executable(miaow_summa main.cpp)

target_compile_features(miaow_summa PRIVATE cxx_std_20)
target_link_libraries(miaow_summa PRIVATE miaow nlohmann_json::nlohmann_json)
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")
miaow_attach_plugin_paths(miaow_summa
    MIAOW_SUMMA_PLUGIN_PATH summabmi)
add_dependencies(miaow_summa miaow_subprocess mizuroute)
