if(NOT DEFINED HELLO_EXECUTABLE OR HELLO_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "HELLO_EXECUTABLE is required")
endif()
if(NOT DEFINED WORKING_DIRECTORY OR WORKING_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "WORKING_DIRECTORY is required")
endif()
if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    message(FATAL_ERROR "OUTPUT_ROOT is required")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${HELLO_EXECUTABLE}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)

if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "Hello example failed for manifest check\n"
        "stdout:\n${run_output}\n"
        "stderr:\n${run_error}")
endif()

file(GLOB run_directories LIST_DIRECTORIES true "${OUTPUT_ROOT}/run_*")
set(found_manifest FALSE)
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

        if(EXISTS "${job_directory}/job_manifest.json")
            set(found_manifest TRUE)
        endif()

        file(GLOB task_directories LIST_DIRECTORIES true "${job_directory}/task_*")
        foreach(task_directory IN LISTS task_directories)
            if(IS_DIRECTORY "${task_directory}")
                set(found_task_directory TRUE)
            endif()
        endforeach()
    endforeach()
endforeach()

if(NOT found_manifest)
    message(FATAL_ERROR "No job_manifest.json found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()

if(NOT found_task_directory)
    message(FATAL_ERROR "No task_* directory found under ${OUTPUT_ROOT}/run_*/job_*/")
endif()
