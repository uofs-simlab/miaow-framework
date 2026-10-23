# CTest invokes this script from the example's source directory. miaow_add_example_test
# supplies EXECUTABLE (example binary), TEST_EXECUTABLE (Boost.Test checker), WORKFLOW
# (Boost.Test case name), OUTPUT_ROOT (per-case output directory), and optional CASE_FILE.
# The example runs first; the checker then inspects its saved job record.

# Remove previous results so a stale job record cannot satisfy this run's checks
file(REMOVE_RECURSE "${OUTPUT_ROOT}")

# Default to a successful run using built-in example inputs and bounded request times
set(expected_exit_code 0)
set(max_workers 2)
set(task_request_timeout 10)
set(job_request_timeout 20)
set(example_arguments)
set(check_arguments)
if(CASE_FILE)
    # Read the expected exit code and example configuration from the regression case
    file(READ "${CASE_FILE}" case)
    string(JSON expected_exit_code GET "${case}" exit_code)
    string(JSON input GET "${case}" input)

    # Pass only the input object to the example; pass the full case file to the checker
    file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
    file(WRITE "${OUTPUT_ROOT}/config.json" "${input}\n")
    list(APPEND example_arguments --config "${OUTPUT_ROOT}/config.json")
    list(APPEND check_arguments "${CASE_FILE}")

    # Apply case overrides; request timeouts are measured in seconds
    foreach(option max_workers task_request_timeout job_request_timeout)
        string(JSON value ERROR_VARIABLE missing GET "${case}" "${option}")
        if(NOT missing)
            set(${option} "${value}")
        endif()
    endforeach()
endif()

# First process: run the example synchronously with its configuration and runtime options
# The 30-second process limit also catches hangs outside the runtime's request timeouts
execute_process(
    COMMAND "${EXECUTABLE}"
        ${example_arguments}
        --max_workers "${max_workers}"
        --task_request_timeout "${task_request_timeout}"
        --job_request_timeout "${job_request_timeout}"
        --output_root "${OUTPUT_ROOT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE example_error
    TIMEOUT 30
)

# Require the exact expected exit code so a crash cannot count as an expected failure
if(NOT status STREQUAL "${expected_exit_code}")
    message(FATAL_ERROR
        "Example exited with ${status}, expected ${expected_exit_code}:\n"
        "${output}\n${example_error}")
endif()
if(CASE_FILE)
    # If the case supplies an error fragment, require it in the captured standard error
    string(JSON expected_error ERROR_VARIABLE missing GET "${case}" error)
    if(NOT missing)
        string(FIND "${example_error}" "${expected_error}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR
                "Missing expected error '${expected_error}':\n${example_error}")
        endif()
    endif()
endif()

# Second process: run a specific test (${WORKFLOW}) in example_regression_checks.cpp
# Boost.Test consumes --run_test; arguments after -- are the job-record path and optional
# case-file path
# Fixture-based tests construct JobRecordChecker before their test-specific assertions
# The examples use run_0/job_0; failure checks also handle an expected missing job record
execute_process(
    COMMAND "${TEST_EXECUTABLE}" "--run_test=${WORKFLOW}"
        -- "${OUTPUT_ROOT}/run_0/job_0/job_record.json" ${check_arguments}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE checks
    ERROR_VARIABLE checker_error
)

# Failed assertions or checker errors make the CTest test fail
if(NOT status STREQUAL "0")
    message(FATAL_ERROR
        "Example regression failed:\n${checks}\n${checker_error}\n"
        "Example output:\n${output}\n${example_error}")
endif()
