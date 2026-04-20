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

# Non-zero exit codes are tolerated: this test asserts the metadata layout
# contract, which must hold even when some tasks in a job fail. Absence of
# metadata or task directories below is still treated as fatal.

file(GLOB run_directories LIST_DIRECTORIES true "${OUTPUT_ROOT}/run_*")
set(found_metadata FALSE)
set(found_task_directory FALSE)

foreach(run_directory IN LISTS run_directories)
    if(NOT IS_DIRECTORY "${run_directory}")
        continue()
    endif()

    file(GLOB job_directories LIST_DIRECTORIES true "${run_directory}/job_*")
    foreach(job_directory IN LISTS job_directories)
        if(NOT IS_DIRECTORY "${job_directory}")
            continue()
        endif()

        if(EXISTS "${job_directory}/job_metadata.json")
            set(found_metadata TRUE)
        endif()

        file(GLOB task_directories LIST_DIRECTORIES true "${job_directory}/task_*")
        foreach(task_directory IN LISTS task_directories)
            if(IS_DIRECTORY "${task_directory}")
                set(found_task_directory TRUE)
            endif()
        endforeach()
    endforeach()
endforeach()

if(NOT found_metadata)
    message(FATAL_ERROR "No job_metadata.json found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()

if(NOT found_task_directory)
    message(FATAL_ERROR "No task_* directory found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()
