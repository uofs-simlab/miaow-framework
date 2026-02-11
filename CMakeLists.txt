cmake_minimum_required(VERSION 3.16)

project(miaow CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MIAOW_USE_SYSTEM_CAF "Prefer a system-installed CAF" ON)
option(MIAOW_FETCH_CAF "Fetch CAF if not found" ON)
set(MIAOW_CAF_VERSION "1.1.0" CACHE STRING "Minimum CAF version for system find_package")
set(MIAOW_CAF_GIT_TAG "1.1.0" CACHE STRING "CAF git tag or commit to fetch")

if(MIAOW_USE_SYSTEM_CAF)
    find_package(CAF ${MIAOW_CAF_VERSION} QUIET COMPONENTS core io)
endif()

if(NOT CAF_FOUND)
    if(MIAOW_FETCH_CAF)
        include(FetchContent)
        message(STATUS "CAF not found. Fetching CAF ${MIAOW_CAF_GIT_TAG}...")
        FetchContent_Declare(
            caf
            GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
            GIT_TAG ${MIAOW_CAF_GIT_TAG}
        )
        FetchContent_MakeAvailable(caf)
    else()
        message(FATAL_ERROR "CAF not found. Install CAF or enable MIAOW_FETCH_CAF.")
    endif()
endif()

add_library(miaow STATIC
    src/miaow.cpp
)

target_include_directories(miaow
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

if(NOT TARGET CAF::core OR NOT TARGET CAF::io)
    message(FATAL_ERROR "CAF targets not available. Ensure CAF is installed with core/io modules.")
endif()

target_link_libraries(miaow
    PUBLIC
        CAF::core
        CAF::io
)

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(miaow_hello
        examples/hello.cpp
    )
    target_link_libraries(miaow_hello PRIVATE miaow)
endif()
