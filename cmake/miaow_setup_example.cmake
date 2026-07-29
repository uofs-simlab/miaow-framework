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

    # Enable CTest and configurable target naming only in standalone builds
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        include(CTest)
        set(EXECUTABLE_TARGET "miaow_${example_name}" CACHE STRING
            "Executable target name for this example")
    else()
        set(EXECUTABLE_TARGET "miaow_${example_name}")
    endif()

    # Add the framework without recursively adding its top-level examples
    if(NOT TARGET miaow)
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

# miaow_add_example_smoke_test(TEST_NAME <name> EXECUTABLE_TARGET <target>
#     [EXTERNAL_MODEL])
#
# Register one successful-run check for an illustrative example workflow
function(miaow_add_example_smoke_test)
    set(options EXTERNAL_MODEL)
    set(one_value_args
        TEST_NAME
        EXECUTABLE_TARGET
    )
    cmake_parse_arguments(MIAOW_TEST
        "${options}" "${one_value_args}" "" ${ARGN})

    # Run the built example without workflow-specific assertions
    add_test(
        NAME ${MIAOW_TEST_TEST_NAME}
        COMMAND $<TARGET_FILE:${MIAOW_TEST_EXECUTABLE_TARGET}>
    )

    # Label every example and identify workflows backed by external models
    set(test_labels example)
    if(MIAOW_TEST_EXTERNAL_MODEL)
        list(APPEND test_labels external_model)
    endif()

    set_tests_properties(${MIAOW_TEST_TEST_NAME} PROPERTIES
        LABELS "${test_labels}"
        TIMEOUT 360
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endfunction()
