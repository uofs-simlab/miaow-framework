# Execute one shared workload in its Boost checker, then inspect the returned record
# and the report written by that same execution. CTest supplies the example directory.

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
# The checker reuses CLI validation and case inputs and verifies expected failures
execute_process(
    COMMAND "${TEST_EXECUTABLE}" "--run_test=${WORKFLOW}"
        -- "${OUTPUT_ROOT}" "${CASE_FILE}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE checks
    ERROR_VARIABLE checker_error
    TIMEOUT 30
)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Workload regression failed:\n${checks}\n${checker_error}")
endif()
