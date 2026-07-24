cmake_minimum_required(VERSION 3.21)

project(miaow VERSION 0.1.0 LANGUAGES CXX Fortran)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MIAOW_BOOST_PROVIDER "auto" CACHE STRING "Boost provider: auto, system, fetch")
set(MIAOW_BOOST_VERSION "1.86.0" CACHE STRING "Boost version for FetchContent")
set_property(CACHE MIAOW_BOOST_PROVIDER PROPERTY STRINGS auto system fetch)
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
miaow_validate_provider(MIAOW_CAF_PROVIDER)

if(NOT MIAOW_BOOST_PROVIDER STREQUAL "fetch")
    find_package(Boost CONFIG QUIET COMPONENTS process json)
    if(NOT TARGET Boost::process OR NOT TARGET Boost::json)
        find_package(Boost QUIET COMPONENTS process json)
    endif()
endif()

if(NOT TARGET Boost::process OR NOT TARGET Boost::json)
    if(MIAOW_BOOST_PROVIDER STREQUAL "system")
        message(FATAL_ERROR
            "Boost.Process or Boost.JSON not found. Use Conan, install Boost, or set "
            "MIAOW_BOOST_PROVIDER to auto or fetch.")
    endif()

    include(FetchContent)
    set(BOOST_INCLUDE_LIBRARIES process json CACHE STRING
        "Boost libraries included by FetchContent" FORCE)
    set(BOOST_ENABLE_CMAKE ON CACHE BOOL "Enable Boost CMake support" FORCE)
    set(miaow_boost_release_url
        "https://github.com/boostorg/boost/releases/download")
    set(miaow_boost_cmake_archive
        "boost-${MIAOW_BOOST_VERSION}-cmake.tar.gz")
    string(CONCAT miaow_boost_url
        "${miaow_boost_release_url}/boost-${MIAOW_BOOST_VERSION}"
        "/${miaow_boost_cmake_archive}")
    message(STATUS "Boost components not found. Fetching Boost ${MIAOW_BOOST_VERSION}...")
    FetchContent_Declare(
        boost
        URL ${miaow_boost_url}
    )
    FetchContent_MakeAvailable(boost)
endif()

if(NOT TARGET Boost::process OR NOT TARGET Boost::json)
    message(FATAL_ERROR "Boost did not provide the expected Boost targets.")
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

# Keep the bridge target while excluding its standalone installation metadata
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
    src/core/actors.cpp
    src/core/process.cpp
    src/core/runtime.cpp
    src/io/cli.cpp
    src/io/csv.cpp
    src/io/diagnostics.cpp
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
        Boost::process
        Boost::json
        iso_c_bmi
        ${CMAKE_DL_LIBS}
)

# Publish build dependencies without carrying their target names into the export
set(miaow_interface_link_libraries
    "$<BUILD_INTERFACE:${MIAOW_CAF_CORE_TARGET}>"
    "$<BUILD_INTERFACE:${MIAOW_CAF_IO_TARGET}>"
    "$<BUILD_INTERFACE:$<LINK_ONLY:Boost::process>>"
    "$<BUILD_INTERFACE:$<LINK_ONLY:Boost::json>>"
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
set_source_files_properties(src/bmi/execute.cpp src/core/process.cpp PROPERTIES
    COMPILE_DEFINITIONS "${MIAOW_SUBPROCESS_COMPILE_DEFINITIONS}"
)

add_executable(miaow_subprocess
    src/bmi/subprocess.cpp
)
target_compile_features(miaow_subprocess PRIVATE cxx_std_20)
target_include_directories(miaow_subprocess PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(miaow_subprocess PRIVATE miaow Boost::process)
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

option(BUILD_EXAMPLES "Build example programs" ON)
set(MIAOW_EXAMPLES
    "00_hello;01_heat;02_soil_freeze_thaw;03_dynamic_bmi;07_job_controller"
    CACHE STRING "Semicolon-separated list of examples to build"
)
set(miaow_known_examples
    00_hello
    01_heat
    02_soil_freeze_thaw
    03_dynamic_bmi
    04_summa
    05_libtrixi_bmi
    07_job_controller
)
set_property(CACHE MIAOW_EXAMPLES PROPERTY STRINGS ${miaow_known_examples})

if(BUILD_EXAMPLES)
    foreach(miaow_example IN LISTS MIAOW_EXAMPLES)
        if(NOT miaow_example IN_LIST miaow_known_examples)
            message(FATAL_ERROR
                "Unknown MIAOW example '${miaow_example}'. Known examples: "
                "${miaow_known_examples}")
        endif()
        add_subdirectory("examples/${miaow_example}")
    endforeach()
endif()
