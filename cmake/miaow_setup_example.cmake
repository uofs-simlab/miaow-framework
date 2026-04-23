# miaow_setup_example(<name> [extra_languages...])
#
# Boilerplate shared by every example CMakeLists.txt. Configures the file to
# work both standalone (its own project()) and bundled into the parent miaow
# build via add_subdirectory(). After this call the caller can rely on:
#   - MIAOW_ROOT      cache PATH pointing at the miaow source tree
#   - EXECUTABLE_TARGET cache STRING set to "miaow_<name>"
#   - the `miaow` target being available
#
# CXX is always enabled; pass extra languages (e.g. Fortran) as positional
# arguments. Must be called as a macro because project() needs file scope.
macro(miaow_setup_example example_name)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(miaow_${example_name}_example CXX ${ARGN})
    endif()

    if(NOT DEFINED MIAOW_ROOT)
        get_filename_component(_miaow_root_default
            "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        set(MIAOW_ROOT "${_miaow_root_default}" CACHE PATH
            "Path to the miaow-framework source root")
        unset(_miaow_root_default)
    endif()

    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(EXECUTABLE_TARGET "miaow_${example_name}" CACHE STRING
            "Executable target name for this example")
    else()
        set(EXECUTABLE_TARGET "miaow_${example_name}")
    endif()

    # Force BUILD_EXAMPLES off in the nested miaow build so it does not
    # recursively re-add this same example directory.
    if(NOT TARGET miaow)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
        add_subdirectory("${MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
    endif()
endmacro()
