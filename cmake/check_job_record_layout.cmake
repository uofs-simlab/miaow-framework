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

# Non-zero exit codes are tolerated: this test asserts the job record layout
# contract, which must hold even when some tasks in a job fail. Absence of
# a job record or task directories below is still treated as fatal.

file(GLOB run_directories LIST_DIRECTORIES true "${OUTPUT_ROOT}/run_*")
set(found_job_record FALSE)
set(found_task_directory FALSE)
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
                set(found_task_directory TRUE)
            endif()
        endforeach()
    endforeach()
endforeach()

if(NOT found_job_record)
    message(FATAL_ERROR "No job_record.json found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()

if(NOT found_task_directory)
    message(FATAL_ERROR "No task_* directory found under ${OUTPUT_ROOT}/run_*/job_*/")
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
