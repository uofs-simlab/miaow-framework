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

# miaow_add_example_test(TEST_NAME <name> EXECUTABLE_TARGET <target>
#     [REGRESSION <workflow>] [CASE_FILE <json>] [EXTERNAL_MODEL])
#
# Run the example and optionally check its saved results against a known outcome
function(miaow_add_example_test)
    set(options EXTERNAL_MODEL)
    set(one_value_args
        TEST_NAME
        EXECUTABLE_TARGET
        REGRESSION
        CASE_FILE
    )
    cmake_parse_arguments(MIAOW_TEST
        "${options}" "${one_value_args}" "" ${ARGN})

    set(test_labels example)
    if(MIAOW_TEST_REGRESSION)
        add_test(NAME ${MIAOW_TEST_TEST_NAME}
            COMMAND ${CMAKE_COMMAND}
                "-DEXECUTABLE=$<TARGET_FILE:${MIAOW_TEST_EXECUTABLE_TARGET}>"
                "-DTEST_EXECUTABLE=$<TARGET_FILE:miaow_example_tests>"
                "-DWORKFLOW=${MIAOW_TEST_REGRESSION}"
                "-DCASE_FILE=${MIAOW_TEST_CASE_FILE}"
                "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/regression-output/${MIAOW_TEST_TEST_NAME}"
                -P "${PROJECT_SOURCE_DIR}/tests/run_example.cmake"
        )
        add_dependencies(${MIAOW_TEST_EXECUTABLE_TARGET} miaow_example_tests)
        list(APPEND test_labels regression)
        set(timeout 40)
    else()
        add_test(NAME ${MIAOW_TEST_TEST_NAME}
            COMMAND $<TARGET_FILE:${MIAOW_TEST_EXECUTABLE_TARGET}>)
        list(APPEND test_labels optional)
        set(timeout 360)
    endif()

    if(MIAOW_TEST_EXTERNAL_MODEL)
        list(APPEND test_labels external_model)
    endif()

    set_tests_properties(${MIAOW_TEST_TEST_NAME} PROPERTIES
        LABELS "${test_labels}"
        TIMEOUT ${timeout}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endfunction()
