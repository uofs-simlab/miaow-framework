include(CMakeParseArguments)

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

    if(NOT DEFINED MIAOW_DEFAULT_MODEL_BASE_DIR)
        set(MIAOW_DEFAULT_MODEL_BASE_DIR "${MIAOW_ROOT}/third_party" CACHE PATH
            "Default base directory containing external model source trees")
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

# miaow_resolve_model_root(<out-var> [MODEL_ROOT <dir>]
#     [MODEL_LOCAL_SUBDIR <dir>] [MODEL_LABEL <label>])
#
# Resolve the source root for an external model. MODEL_ROOT takes precedence;
# otherwise MODEL_LOCAL_SUBDIR is resolved relative to
# MIAOW_DEFAULT_MODEL_BASE_DIR. The resolved path is returned through <out-var>,
# and MODEL_LABEL is used only for status output.
function(miaow_resolve_model_root out_var)
    set(options)
    set(one_value_args MODEL_ROOT MODEL_LOCAL_SUBDIR MODEL_LABEL)
    cmake_parse_arguments(MIAOW_MODEL "${options}" "${one_value_args}" "" ${ARGN})

    if(MIAOW_MODEL_MODEL_ROOT)
        set(resolved_model_root "${MIAOW_MODEL_MODEL_ROOT}")
    elseif(MIAOW_MODEL_MODEL_LOCAL_SUBDIR)
        set(resolved_model_root
            "${MIAOW_DEFAULT_MODEL_BASE_DIR}/${MIAOW_MODEL_MODEL_LOCAL_SUBDIR}")
    else()
        message(FATAL_ERROR
            "miaow_resolve_model_root: set MODEL_ROOT or MODEL_LOCAL_SUBDIR")
    endif()

    if(MIAOW_MODEL_MODEL_LABEL)
        message(STATUS
            "MIAOW: resolved model root for ${MIAOW_MODEL_MODEL_LABEL}: ${resolved_model_root}")
    else()
        message(STATUS "MIAOW: resolved model root: ${resolved_model_root}")
    endif()

    set(${out_var} "${resolved_model_root}" PARENT_SCOPE)
endfunction()

# miaow_provision_example_extern_link(EXAMPLE_DIR <dir> LINK_NAME <name>
#     TARGET_DIR <dir>)
#
# Ensure EXAMPLE_DIR/extern/LINK_NAME is a symlink to TARGET_DIR. This keeps
# example-local extern paths stable whether the backing model source was
# supplied manually or fetched into third_party/.
#
# Existing symlinks at the destination are replaced. A non-symlink entry at the
# same path is treated as an error to avoid clobbering user files.
function(miaow_provision_example_extern_link)
    set(options)
    set(one_value_args EXAMPLE_DIR LINK_NAME TARGET_DIR)
    cmake_parse_arguments(MIAOW_EXTERN "${options}" "${one_value_args}" "" ${ARGN})

    if(WIN32)
        message(FATAL_ERROR
            "MIAOW extern symlink workflow is POSIX-only (macOS/Linux)")
    endif()
    if(NOT MIAOW_EXTERN_EXAMPLE_DIR OR NOT MIAOW_EXTERN_LINK_NAME OR
            NOT MIAOW_EXTERN_TARGET_DIR)
        message(FATAL_ERROR
            "miaow_provision_example_extern_link: EXAMPLE_DIR, LINK_NAME, and TARGET_DIR are required")
    endif()

    get_filename_component(example_dir_abs "${MIAOW_EXTERN_EXAMPLE_DIR}" ABSOLUTE)
    get_filename_component(target_dir_abs "${MIAOW_EXTERN_TARGET_DIR}" ABSOLUTE)
    set(example_extern_dir "${example_dir_abs}/extern")
    set(example_extern_link_path "${example_extern_dir}/${MIAOW_EXTERN_LINK_NAME}")

    file(MAKE_DIRECTORY "${example_extern_dir}")

    if(EXISTS "${example_extern_link_path}" OR IS_SYMLINK "${example_extern_link_path}")
        if(IS_SYMLINK "${example_extern_link_path}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E rm -f "${example_extern_link_path}"
            )
        else()
            message(FATAL_ERROR
                "MIAOW extern link path exists and is not a symlink: ${example_extern_link_path}")
        endif()
    endif()

    file(CREATE_LINK "${target_dir_abs}" "${example_extern_link_path}" SYMBOLIC)
    message(STATUS
        "MIAOW: linked ${example_extern_link_path} -> ${target_dir_abs}")
endfunction()
