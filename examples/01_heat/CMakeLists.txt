cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_heat_example CXX)
endif()

get_filename_component(MIAOW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET miaow)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
    add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
endif()

add_executable(miaow_heat
    bmi_main.cxx
    heat.cxx
    bmi_heat.cxx
)

target_include_directories(miaow_heat
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(miaow_heat PRIVATE miaow)

set(miaow_heat_output_dir "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(miaow_heat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${miaow_heat_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${miaow_heat_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${miaow_heat_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${miaow_heat_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${miaow_heat_output_dir}"
)
