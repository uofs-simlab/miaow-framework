cmake_minimum_required(VERSION 3.16)

project(miaow CXX Fortran)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

set(MIAOW_BOOST_PROVIDER "system" CACHE STRING "Boost provider: auto, system, fetch")
set(MIAOW_BOOST_VERSION "1.86.0" CACHE STRING "Boost version for FetchContent")
set_property(CACHE MIAOW_BOOST_PROVIDER PROPERTY STRINGS auto system fetch)
set(MIAOW_CAF_PROVIDER "auto" CACHE STRING "CAF provider: auto, system, fetch")
set_property(CACHE MIAOW_CAF_PROVIDER PROPERTY STRINGS auto system fetch)
set(MIAOW_CAF_VERSION "1.1.0" CACHE STRING "Minimum CAF version for system find_package")
set(MIAOW_CAF_GIT_TAG "1.1.0" CACHE STRING "CAF git tag or commit to fetch")
set(MIAOW_DEFAULT_MODEL_BASE_DIR "${PROJECT_SOURCE_DIR}/third_party" CACHE PATH
    "Default base directory containing external model source trees")
set(MIAOW_ISO_C_FORTRAN_BMI_ROOT "" CACHE PATH
    "Path to a local NOAA-OWP/iso_c_fortran_bmi checkout")

function(miaow_validate_provider provider_name)
    set(valid_providers auto system fetch)
    set(provider_value "${${provider_name}}")
    if(NOT provider_value IN_LIST valid_providers)
        message(FATAL_ERROR
            "${provider_name} must be one of: auto, system, fetch")
    endif()
endfunction()

miaow_validate_provider(MIAOW_BOOST_PROVIDER)
miaow_validate_provider(MIAOW_CAF_PROVIDER)

if(NOT MIAOW_BOOST_PROVIDER STREQUAL "fetch")
    find_package(Boost CONFIG QUIET COMPONENTS process)
    if(NOT TARGET Boost::process)
        find_package(Boost QUIET COMPONENTS process)
    endif()
endif()

if(NOT TARGET Boost::process)
    if(MIAOW_BOOST_PROVIDER STREQUAL "system")
        message(FATAL_ERROR
            "Boost.Process not found. Use Conan, install Boost, or set "
            "MIAOW_BOOST_PROVIDER to auto or fetch.")
    endif()

    include(FetchContent)
    set(BOOST_INCLUDE_LIBRARIES process CACHE STRING
        "Boost libraries included by FetchContent" FORCE)
    set(BOOST_ENABLE_CMAKE ON CACHE BOOL "Enable Boost CMake support" FORCE)
    set(miaow_boost_release_url
        "https://github.com/boostorg/boost/releases/download")
    set(miaow_boost_cmake_archive
        "boost-${MIAOW_BOOST_VERSION}-cmake.tar.gz")
    string(CONCAT miaow_boost_url
        "${miaow_boost_release_url}/boost-${MIAOW_BOOST_VERSION}"
        "/${miaow_boost_cmake_archive}")
    message(STATUS "Boost.Process not found. Fetching Boost ${MIAOW_BOOST_VERSION}...")
    FetchContent_Declare(
        boost
        URL ${miaow_boost_url}
    )
    FetchContent_MakeAvailable(boost)
endif()

if(NOT TARGET Boost::process)
    message(FATAL_ERROR "Boost did not provide the expected Boost::process target.")
endif()

set(MIAOW_CAF_CORE_TARGET "")
set(MIAOW_CAF_IO_TARGET "")

function(miaow_set_caf_targets)
    if(TARGET CAF::core AND TARGET CAF::io)
        set(MIAOW_CAF_CORE_TARGET "CAF::core" PARENT_SCOPE)
        set(MIAOW_CAF_IO_TARGET "CAF::io" PARENT_SCOPE)
    elseif(TARGET caf::core AND TARGET caf::io)
        set(MIAOW_CAF_CORE_TARGET "caf::core" PARENT_SCOPE)
        set(MIAOW_CAF_IO_TARGET "caf::io" PARENT_SCOPE)
    elseif(TARGET caf::caf_core AND TARGET caf::caf_io)
        set(MIAOW_CAF_CORE_TARGET "caf::caf_core" PARENT_SCOPE)
        set(MIAOW_CAF_IO_TARGET "caf::caf_io" PARENT_SCOPE)
    endif()
endfunction()

if(NOT MIAOW_CAF_PROVIDER STREQUAL "fetch")
    find_package(CAF ${MIAOW_CAF_VERSION} QUIET COMPONENTS core io)
    miaow_set_caf_targets()
    if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
        find_package(caf ${MIAOW_CAF_VERSION} QUIET COMPONENTS core io)
        miaow_set_caf_targets()
    endif()
endif()

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    if(MIAOW_CAF_PROVIDER STREQUAL "system")
        message(FATAL_ERROR
            "CAF not found. Use Conan, install CAF, or set MIAOW_CAF_PROVIDER "
            "to auto or fetch.")
    endif()

    include(FetchContent)
    # Keep CAF fetch/build lean in CI and local fallback builds.
    set(CAF_BUILD_EXAMPLES OFF CACHE BOOL "Disable CAF examples" FORCE)
    set(CAF_ENABLE_EXAMPLES OFF CACHE BOOL "Disable CAF examples" FORCE)
    set(CAF_BUILD_TESTS OFF CACHE BOOL "Disable CAF tests" FORCE)
    set(CAF_BUILD_TESTING OFF CACHE BOOL "Disable CAF tests" FORCE)
    set(CAF_ENABLE_TESTING OFF CACHE BOOL "Disable CAF tests" FORCE)
    set(CAF_BUILD_BENCHMARKS OFF CACHE BOOL "Disable CAF benchmarks" FORCE)
    set(CAF_ENABLE_BENCHMARKS OFF CACHE BOOL "Disable CAF benchmarks" FORCE)
    set(CAF_BUILD_TOOLS OFF CACHE BOOL "Disable CAF tools" FORCE)
    message(STATUS "CAF not found. Fetching CAF ${MIAOW_CAF_GIT_TAG}...")
    FetchContent_Declare(
        caf
        GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
        GIT_TAG ${MIAOW_CAF_GIT_TAG}
    )
    FetchContent_MakeAvailable(caf)
    miaow_set_caf_targets()
endif()

if(MIAOW_ISO_C_FORTRAN_BMI_ROOT)
    message(STATUS "MIAOW: using iso_c_fortran_bmi source root ${MIAOW_ISO_C_FORTRAN_BMI_ROOT}")
    if(NOT EXISTS "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "MIAOW_ISO_C_FORTRAN_BMI_ROOT must point to a NOAA-OWP/iso_c_fortran_bmi checkout")
    endif()
    add_subdirectory(
        "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/iso_c_fortran_bmi-build"
    )
else()
    set(iso_c_fortran_bmi_source_dir "${MIAOW_DEFAULT_MODEL_BASE_DIR}/iso_c_fortran_bmi")
    message(STATUS "MIAOW: using iso_c_fortran_bmi source root ${iso_c_fortran_bmi_source_dir}")
    if(EXISTS "${iso_c_fortran_bmi_source_dir}/CMakeLists.txt")
        add_subdirectory(
            "${iso_c_fortran_bmi_source_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}/iso_c_fortran_bmi-build"
        )
    else()
        include(FetchContent)
        FetchContent_Declare(
            iso_c_fortran_bmi
            GIT_REPOSITORY https://github.com/NOAA-OWP/iso_c_fortran_bmi.git
            GIT_TAG "fad31177d5a10b3773db528d117e7ff17159fb07"
            SOURCE_DIR "${iso_c_fortran_bmi_source_dir}"
            BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/iso_c_fortran_bmi-build"
        )
        FetchContent_MakeAvailable(iso_c_fortran_bmi)
    endif()
endif()

if(NOT TARGET iso_c_bmi)
    message(FATAL_ERROR
        "NOAA-OWP/iso_c_fortran_bmi did not provide the expected iso_c_bmi target")
endif()

add_library(miaow STATIC
    src/bmi/execute.cpp
    src/bmi/dynamic.cpp
    src/bmi/forcing.cpp
    src/bmi/output.cpp
    src/bmi/subprocess.cpp
    src/core/actors.cpp
    src/core/runtime.cpp
    src/io/cli.cpp
    src/io/csv.cpp
    src/io/diagnostics.cpp
    src/io/metadata.cpp
    src/io/realization.cpp
)

target_include_directories(miaow
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    message(FATAL_ERROR "CAF targets not available. Ensure CAF is installed with core/io modules.")
endif()

target_link_libraries(miaow
    PUBLIC
        ${MIAOW_CAF_CORE_TARGET}
        ${MIAOW_CAF_IO_TARGET}
        iso_c_bmi
        ${CMAKE_DL_LIBS}
    PRIVATE
        Boost::process
)

set(MIAOW_SUBPROCESS_PATH
    "${PROJECT_BINARY_DIR}/miaow_subprocess${CMAKE_EXECUTABLE_SUFFIX}")
set_source_files_properties(src/bmi/subprocess.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAOW_SUBPROCESS_PATH=\"${MIAOW_SUBPROCESS_PATH}\""
)

add_executable(miaow_subprocess
    src/bmi/subprocess_main.cpp
)
target_compile_features(miaow_subprocess PRIVATE cxx_std_17)
target_include_directories(miaow_subprocess PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(miaow_subprocess PRIVATE miaow)
set_target_properties(miaow_subprocess PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_subdirectory(examples/00_hello)
    add_subdirectory(examples/01_heat)
    add_subdirectory(examples/02_soil_freeze_thaw)
    add_subdirectory(examples/03_dynamic_bmi)
    add_subdirectory(examples/04_noah_owp)
endif()
