include(CMakeParseArguments)

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

    # Register the executable as a successful-run smoke test
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
