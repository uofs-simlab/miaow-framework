cmake_minimum_required(VERSION 3.16)

project(miaow CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(miaow STATIC
    src/miaow.cpp
)

target_include_directories(miaow
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(miaow_hello
        examples/hello.cpp
    )
    target_link_libraries(miaow_hello PRIVATE miaow)
endif()
