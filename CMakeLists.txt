cmake_minimum_required(VERSION 3.16)

project(miaow C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(miaow STATIC
    src/miaow.c
)

target_include_directories(miaow
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(miaow_hello
        examples/hello.c
    )
    target_link_libraries(miaow_hello PRIVATE miaow)
endif()
