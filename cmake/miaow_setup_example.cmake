include(CMakeParseArguments)

# miaow_setup_example(<name>)
#
# Boilerplate shared by every example CMakeLists.txt after its optional
# standalone project declaration. After this call the caller can rely on:
#   - MIAOW_ROOT      cache PATH pointing at the miaow source tree
#   - EXECUTABLE_TARGET cache STRING set to "miaow_<name>"
#   - the `miaow` target being available
macro(miaow_setup_example example_name)
    # Resolve the framework root relative to this helper unless supplied
    if(NOT DEFINED MIAOW_ROOT)
        get_filename_component(_miaow_root_default
            "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        set(MIAOW_ROOT "${_miaow_root_default}" CACHE PATH
            "Path to the miaow-framework source root")
        unset(_miaow_root_default)
    endif()

    # Use the framework third_party directory for model sources by default
    if(NOT DEFINED MIAOW_DEFAULT_MODEL_BASE_DIR)
        set(MIAOW_DEFAULT_MODEL_BASE_DIR "${MIAOW_ROOT}/third_party" CACHE PATH
            "Default base directory containing external model source trees")
    endif()

    # Keep the executable name configurable only in standalone builds
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

    # Prefer an explicit checkout over the default model directory
    if(MIAOW_MODEL_MODEL_ROOT)
        set(resolved_model_root "${MIAOW_MODEL_MODEL_ROOT}")
    elseif(MIAOW_MODEL_MODEL_LOCAL_SUBDIR)
        set(resolved_model_root
            "${MIAOW_DEFAULT_MODEL_BASE_DIR}/${MIAOW_MODEL_MODEL_LOCAL_SUBDIR}")
    else()
        message(FATAL_ERROR
            "miaow_resolve_model_root: set MODEL_ROOT or MODEL_LOCAL_SUBDIR")
    endif()

    # Report the selected source path during configuration
    if(MIAOW_MODEL_MODEL_LABEL)
        message(STATUS
            "MIAOW: resolved model root for ${MIAOW_MODEL_MODEL_LABEL}: "
            "${resolved_model_root}")
    else()
        message(STATUS "MIAOW: resolved model root: ${resolved_model_root}")
    endif()

    set(${out_var} "${resolved_model_root}" PARENT_SCOPE)
endfunction()

# miaow_add_workflow_test(TEST_NAME <name> EXECUTABLE_TARGET <target>
#     [WORKING_DIRECTORY <dir>] [PASS_REGULAR_EXPRESSION <regex>]
#     [RESOURCE_LOCK <lock>] [CLI_ARGS <arg>...])
#
# Register an example executable as a CTest workflow
function(miaow_add_workflow_test)
    set(options)
    set(one_value_args
        TEST_NAME
        EXECUTABLE_TARGET
        WORKING_DIRECTORY
        PASS_REGULAR_EXPRESSION
        RESOURCE_LOCK
    )
    set(multi_value_args CLI_ARGS)
    cmake_parse_arguments(MIAOW_TEST
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # Forward CLI arguments directly to the built example
    add_test(
        NAME ${MIAOW_TEST_TEST_NAME}
        COMMAND
            $<TARGET_FILE:${MIAOW_TEST_EXECUTABLE_TARGET}>
            ${MIAOW_TEST_CLI_ARGS}
    )

    # Collect only the optional CTest properties supplied by the caller
    set(test_properties)
    if(MIAOW_TEST_WORKING_DIRECTORY)
        list(APPEND test_properties
            WORKING_DIRECTORY "${MIAOW_TEST_WORKING_DIRECTORY}")
    endif()
    if(MIAOW_TEST_PASS_REGULAR_EXPRESSION)
        list(APPEND test_properties
            PASS_REGULAR_EXPRESSION "${MIAOW_TEST_PASS_REGULAR_EXPRESSION}")
    endif()
    if(MIAOW_TEST_RESOURCE_LOCK)
        list(APPEND test_properties RESOURCE_LOCK "${MIAOW_TEST_RESOURCE_LOCK}")
    endif()
    if(test_properties)
        set_tests_properties(${MIAOW_TEST_TEST_NAME} PROPERTIES ${test_properties})
    endif()
endfunction()
