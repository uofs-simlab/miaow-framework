if(NOT DEFINED EXECUTABLE OR EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED WORKING_DIRECTORY OR WORKING_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "WORKING_DIRECTORY is required")
endif()
if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    message(FATAL_ERROR "OUTPUT_ROOT is required")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${EXECUTABLE}" ${EXECUTABLE_ARGS}
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)

if(DEFINED CHECK_SECOND_RUN AND CHECK_SECOND_RUN)
    execute_process(
        COMMAND "${EXECUTABLE}" ${EXECUTABLE_ARGS}
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        OUTPUT_VARIABLE second_run_output
        ERROR_VARIABLE second_run_error
    )
endif()

# Non-zero exit codes are tolerated because this test asserts the output layout
# contract, which must hold even when some tasks in a job fail

if(NOT IS_DIRECTORY "${OUTPUT_ROOT}/run_0")
    message(FATAL_ERROR "Expected ${OUTPUT_ROOT}/run_0 to exist")
endif()

if(DEFINED CHECK_SECOND_RUN AND CHECK_SECOND_RUN AND
        NOT IS_DIRECTORY "${OUTPUT_ROOT}/run_1")
    message(FATAL_ERROR "Expected ${OUTPUT_ROOT}/run_1 to exist after second run")
endif()

file(GLOB run_directories LIST_DIRECTORIES true "${OUTPUT_ROOT}/run_*")
set(found_job_record FALSE)
set(job_record_paths)

foreach(run_directory IN LISTS run_directories)
    if(NOT IS_DIRECTORY "${run_directory}")
        continue()
    endif()

    file(GLOB job_directories LIST_DIRECTORIES true "${run_directory}/job_*")
    foreach(job_directory IN LISTS job_directories)
        if(NOT IS_DIRECTORY "${job_directory}")
            continue()
        endif()

        if(EXISTS "${job_directory}/job_record.json")
            set(found_job_record TRUE)
            list(APPEND job_record_paths "${job_directory}/job_record.json")
        endif()

        file(GLOB task_directories LIST_DIRECTORIES true "${job_directory}/task_*")
        foreach(task_directory IN LISTS task_directories)
            if(IS_DIRECTORY "${task_directory}")
                message(FATAL_ERROR
                    "Unexpected task_* directory found under ${job_directory}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(NOT found_job_record)
    message(FATAL_ERROR "No job_record.json found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()

set(required_job_record_snippets
    "\"metadata\""
    "\"result\""
    "\"artifact_paths\""
    "\"status\""
    "\"run_time_s\""
)
if(DEFINED EXPECTED_METADATA_SNIPPETS)
    list(APPEND required_job_record_snippets ${EXPECTED_METADATA_SNIPPETS})
endif()

foreach(job_record_path IN LISTS job_record_paths)
    file(READ "${job_record_path}" job_record_text)
    foreach(required_job_record_snippet IN LISTS required_job_record_snippets)
        string(FIND "${job_record_text}" "${required_job_record_snippet}" snippet_index)
        if(snippet_index EQUAL -1)
            message(FATAL_ERROR
                "${job_record_path} does not contain ${required_job_record_snippet}")
        endif()
    endforeach()
endforeach()

if(DEFINED EXPECTED_RESULT_PATHS)
    foreach(expected_result_path IN LISTS EXPECTED_RESULT_PATHS)
        if(NOT EXISTS "${OUTPUT_ROOT}/run_0/job_0/${expected_result_path}")
            message(FATAL_ERROR
                "Expected result file not found: "
                "${OUTPUT_ROOT}/run_0/job_0/${expected_result_path}")
        endif()
    endforeach()
endif()
