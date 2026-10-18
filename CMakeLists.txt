cmake_minimum_required(VERSION 3.21)

project(miaow VERSION 0.2.0 LANGUAGES CXX Fortran)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Add framework tests to top-level development builds
set(miaow_build_tests OFF)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    set(miaow_build_tests ON)
endif()

set(MIAOW_BOOST_PROVIDER "auto" CACHE STRING "Boost provider: auto, system, fetch")
set(MIAOW_BOOST_VERSION "1.86.0" CACHE STRING "Boost version for FetchContent")
set_property(CACHE MIAOW_BOOST_PROVIDER PROPERTY STRINGS auto system fetch)
set(MIAOW_JSON_PROVIDER "auto" CACHE STRING "nlohmann/json provider: auto, system, fetch")
set_property(CACHE MIAOW_JSON_PROVIDER PROPERTY STRINGS auto system fetch)
set(MIAOW_JSON_VERSION "3.12.0" CACHE STRING "nlohmann/json version")
set(MIAOW_CAF_PROVIDER "auto" CACHE STRING "CAF provider: auto, system, fetch")
set_property(CACHE MIAOW_CAF_PROVIDER PROPERTY STRINGS auto system fetch)
set(MIAOW_CAF_VERSION "1.1.0" CACHE STRING "CAF version")
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
miaow_validate_provider(MIAOW_JSON_PROVIDER)
miaow_validate_provider(MIAOW_CAF_PROVIDER)

# Boost is required only for the framework tests
if(miaow_build_tests)
    if(NOT MIAOW_BOOST_PROVIDER STREQUAL "fetch")
        find_package(Boost CONFIG QUIET COMPONENTS unit_test_framework)
        if(NOT TARGET Boost::unit_test_framework)
            find_package(Boost QUIET COMPONENTS unit_test_framework)
        endif()
    endif()

    if(NOT TARGET Boost::unit_test_framework)
        if(MIAOW_BOOST_PROVIDER STREQUAL "system")
            message(FATAL_ERROR
                "Boost.Test not found. Use Conan, install Boost, or set "
                "MIAOW_BOOST_PROVIDER to auto or fetch.")
        endif()

        include(FetchContent)
        set(BOOST_INCLUDE_LIBRARIES test CACHE STRING
            "Boost libraries included by FetchContent" FORCE)
        set(BOOST_ENABLE_CMAKE ON CACHE BOOL "Enable Boost CMake support" FORCE)
        string(CONCAT miaow_boost_url
            "https://github.com/boostorg/boost/releases/download/boost-"
            "${MIAOW_BOOST_VERSION}/boost-${MIAOW_BOOST_VERSION}-cmake.tar.gz")
        message(STATUS "Boost.Test not found. Fetching Boost ${MIAOW_BOOST_VERSION}...")
        FetchContent_Declare(boost URL ${miaow_boost_url})
        FetchContent_MakeAvailable(boost)
    endif()
endif()

# JSON text parsing and formatting use one private, header-only dependency
if(NOT MIAOW_JSON_PROVIDER STREQUAL "fetch")
    find_package(nlohmann_json ${MIAOW_JSON_VERSION} QUIET CONFIG)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    if(MIAOW_JSON_PROVIDER STREQUAL "system")
        message(FATAL_ERROR
            "nlohmann/json not found. Use Conan, install nlohmann/json, or set "
            "MIAOW_JSON_PROVIDER to auto or fetch.")
    endif()
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann/json tests" FORCE)
    set(JSON_Install OFF CACHE BOOL "Disable nlohmann/json installation" FORCE)
    message(STATUS "nlohmann/json not found. Fetching ${MIAOW_JSON_VERSION}...")
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v${MIAOW_JSON_VERSION}/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
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
    message(STATUS "CAF not found. Fetching CAF ${MIAOW_CAF_VERSION}...")
    FetchContent_Declare(
        caf
        GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
        GIT_TAG ${MIAOW_CAF_VERSION}
    )
    FetchContent_MakeAvailable(caf)
    miaow_set_caf_targets()
endif()

if(MIAOW_ISO_C_FORTRAN_BMI_ROOT)
    message(STATUS
        "MIAOW: using iso_c_fortran_bmi source root ${MIAOW_ISO_C_FORTRAN_BMI_ROOT}")
    if(NOT EXISTS "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "MIAOW_ISO_C_FORTRAN_BMI_ROOT must point to a "
            "NOAA-OWP/iso_c_fortran_bmi checkout")
    endif()
    add_subdirectory(
        "${MIAOW_ISO_C_FORTRAN_BMI_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/iso_c_fortran_bmi-build"
    )
else()
    set(iso_c_fortran_bmi_source_dir "${MIAOW_DEFAULT_MODEL_BASE_DIR}/iso_c_fortran_bmi")
    message(STATUS
        "MIAOW: using iso_c_fortran_bmi source root ${iso_c_fortran_bmi_source_dir}")
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

# Exclude the bridge subproject's package metadata from MIAOW installation
get_target_property(miaow_iso_c_bmi_source_dir iso_c_bmi SOURCE_DIR)
set_property(DIRECTORY "${miaow_iso_c_bmi_source_dir}"
    PROPERTY EXCLUDE_FROM_ALL TRUE)

add_library(miaow STATIC
    src/bmi/execute.cpp
    src/bmi/dynamic.cpp
    src/bmi/forcing.cpp
    src/bmi/io.cpp
    src/bmi/pipeline.cpp
    src/bmi/realization.cpp
    src/bmi/variable.cpp
    src/core/actors.cpp
    src/core/process.cpp
    src/core/runtime.cpp
    src/io/cli.cpp
    src/io/csv.cpp
    src/io/diagnostics.cpp
    src/io/json.cpp
)

target_include_directories(miaow
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_compile_features(miaow PUBLIC cxx_std_20)

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    message(FATAL_ERROR
        "CAF targets not available. Ensure CAF is installed with core/io modules.")
endif()

target_link_libraries(miaow
    PRIVATE
        ${MIAOW_CAF_CORE_TARGET}
        ${MIAOW_CAF_IO_TARGET}
        nlohmann_json::nlohmann_json
        iso_c_bmi
        ${CMAKE_DL_LIBS}
)

# Publish build dependencies without carrying their target names into the export
set(miaow_interface_link_libraries
    "$<BUILD_INTERFACE:${MIAOW_CAF_CORE_TARGET}>"
    "$<BUILD_INTERFACE:${MIAOW_CAF_IO_TARGET}>"
    "$<BUILD_INTERFACE:$<LINK_ONLY:iso_c_bmi>>"
)
if(CMAKE_DL_LIBS)
    list(APPEND miaow_interface_link_libraries "${CMAKE_DL_LIBS}")
endif()
set_property(TARGET miaow PROPERTY INTERFACE_LINK_LIBRARIES
    "${miaow_interface_link_libraries}")

set(MIAOW_SUBPROCESS_PATH
    "${PROJECT_BINARY_DIR}/miaow_subprocess${CMAKE_EXECUTABLE_SUFFIX}")
string(CONCAT MIAOW_INSTALLED_SUBPROCESS_PATH
    "${CMAKE_INSTALL_FULL_LIBEXECDIR}/miaow/"
    "miaow_subprocess${CMAKE_EXECUTABLE_SUFFIX}")
string(CONCAT MIAOW_SUBPROCESS_COMPILE_DEFINITIONS
    "MIAOW_SUBPROCESS_PATH=\"${MIAOW_SUBPROCESS_PATH}\";"
    "MIAOW_INSTALLED_SUBPROCESS_PATH=\"${MIAOW_INSTALLED_SUBPROCESS_PATH}\"")
set_source_files_properties(src/bmi/execute.cpp PROPERTIES
    COMPILE_DEFINITIONS "${MIAOW_SUBPROCESS_COMPILE_DEFINITIONS}"
)

add_executable(miaow_subprocess
    src/bmi/subprocess.cpp
)
target_compile_features(miaow_subprocess PRIVATE cxx_std_20)
target_include_directories(miaow_subprocess PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(miaow_subprocess PRIVATE miaow nlohmann_json::nlohmann_json)
set_target_properties(miaow_subprocess PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

# Resolve the installed BMI bridge relative to the subprocess executable
file(RELATIVE_PATH miaow_subprocess_libdir_relative
    "${CMAKE_INSTALL_FULL_LIBEXECDIR}/miaow"
    "${CMAKE_INSTALL_FULL_LIBDIR}"
)
if(APPLE)
    set(miaow_subprocess_install_rpath
        "@loader_path/${miaow_subprocess_libdir_relative}")
elseif(UNIX)
    set(miaow_subprocess_install_rpath
        "\$ORIGIN/${miaow_subprocess_libdir_relative}")
endif()
if(miaow_subprocess_install_rpath)
    set_target_properties(miaow_subprocess PROPERTIES
        INSTALL_RPATH "${miaow_subprocess_install_rpath}"
    )
endif()

# Install the public library interface and runtime helper
install(TARGETS miaow
    EXPORT miaowTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS iso_c_bmi
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS miaow_subprocess
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miaow
)
install(FILES
    include/miaow.hpp
    include/miaow_bmi.hpp
    include/miaow_io.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES include/third_party/bmi/bmi.hxx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/third_party/bmi
)

# Install the project and third-party licences
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/miaow
)
install(FILES include/third_party/bmi/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/miaow
    RENAME BMI-LICENSE
)
install(FILES "${miaow_iso_c_bmi_source_dir}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/miaow
    RENAME ISO_C_FORTRAN_BMI-LICENSE
)

# Install the CMake package without a package-version compatibility file
set(miaow_package_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/miaow")
configure_package_config_file(
    cmake/miaowConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/miaowConfig.cmake"
    INSTALL_DESTINATION "${miaow_package_install_dir}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/miaowConfig.cmake"
    DESTINATION "${miaow_package_install_dir}"
)
install(EXPORT miaowTargets
    FILE miaowTargets.cmake
    NAMESPACE miaow::
    DESTINATION "${miaow_package_install_dir}"
)

if(miaow_build_tests)
    add_subdirectory(tests)
endif()

# Add examples selected by the repository build
if(PROJECT_IS_TOP_LEVEL)
    option(BUILD_EXAMPLES "Build example programs" OFF)
    set(MIAOW_EXAMPLES
        "00_hello;01_heat;02_soil_freeze_thaw;03_dynamic_bmi;07_job_controller"
        CACHE STRING "Semicolon-separated list of examples to build"
    )

    if(BUILD_EXAMPLES)
        include(cmake/miaow_example.cmake)
        foreach(miaow_example IN LISTS MIAOW_EXAMPLES)
            add_subdirectory("examples/${miaow_example}")
        endforeach()
    endif()
endif()
