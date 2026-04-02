cmake_minimum_required(VERSION 3.16)

project(miaow CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

option(MIAOW_USE_SYSTEM_CAF "Prefer a system-installed CAF" ON)
option(MIAOW_FETCH_CAF "Fetch CAF if not found" ON)
set(MIAOW_CAF_VERSION "1.1.0" CACHE STRING "Minimum CAF version for system find_package")
set(MIAOW_CAF_GIT_TAG "1.1.0" CACHE STRING "CAF git tag or commit to fetch")

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

if(MIAOW_USE_SYSTEM_CAF)
    find_package(CAF ${MIAOW_CAF_VERSION} QUIET COMPONENTS core io)
    miaow_set_caf_targets()
    if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
        find_package(caf ${MIAOW_CAF_VERSION} QUIET COMPONENTS core io)
        miaow_set_caf_targets()
    endif()
endif()

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    if(NOT MIAOW_FETCH_CAF)
        message(FATAL_ERROR "CAF not found via find_package. Install CAF or enable MIAOW_FETCH_CAF.")
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

add_library(miaow STATIC
    src/bmi/forcing.cpp
    src/bmi/workflow.cpp
    src/core/actors.cpp
    src/core/diagnostics.cpp
    src/core/runtime.cpp
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
)

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_subdirectory(examples/00_hello)
    add_subdirectory(examples/01_heat)
    add_subdirectory(examples/02_soil_freeze_thaw)
endif()
