include(CMakeParseArguments)

# miaow_add_bmi_model([EXAMPLE_NAME <name>] [MIAOW_ROOT <path>]
#     [MODEL_ROOT <dir> | MODEL_LOCAL_SUBDIR <dir>]
#     [MODEL_GIT_REPOSITORY <url> MODEL_GIT_TAG <rev>]
#     [MODEL_FETCH_NAME <name>] [EXECUTABLE_TARGET <target>]
#     [MODEL_TARGET <target>] [OUTPUT_DIR <dir>]
#     [EXECUTABLE_SOURCES <src>...] [MODEL_SOURCES <src>...]
#     [MODEL_INCLUDE_DIRS <dir>...]
#     [MODEL_COMPILE_DEFINITIONS <def>...]
#     [MODEL_LINK_LIBRARIES <lib>...] [USE_MIAOW_BMI_INCLUDE])
#
# Build an example-local static BMI model target and an executable that links
# it with miaow.
#
# Model source discovery follows this order:
#   1. explicit MODEL_ROOT
#   2. MODEL_LOCAL_SUBDIR under the default model base directory
#   3. fetch from MODEL_GIT_REPOSITORY at MODEL_GIT_TAG if the resolved path
#      does not exist yet
#
# Relative MODEL_SOURCES and MODEL_INCLUDE_DIRS are interpreted relative to the
# resolved model root. Resolved target names and the final model root are
# exported back to the caller via parent-scope variables.
function(miaow_add_bmi_model)
    set(options USE_MIAOW_BMI_INCLUDE)
    set(one_value_args
        EXAMPLE_NAME
        MIAOW_ROOT
        MODEL_ROOT
        MODEL_LOCAL_SUBDIR
        MODEL_GIT_REPOSITORY
        MODEL_GIT_TAG
        MODEL_FETCH_NAME
        EXECUTABLE_TARGET
        MODEL_TARGET
        OUTPUT_DIR
    )
    set(multi_value_args
        EXECUTABLE_SOURCES
        MODEL_SOURCES
        MODEL_INCLUDE_DIRS
        MODEL_COMPILE_DEFINITIONS
        MODEL_LINK_LIBRARIES
    )
    cmake_parse_arguments(MIAOW "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MIAOW_MIAOW_ROOT)
        get_filename_component(MIAOW_MIAOW_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    endif()

    if(NOT MIAOW_EXECUTABLE_TARGET)
        if(MIAOW_EXAMPLE_NAME)
            set(MIAOW_EXECUTABLE_TARGET "miaow_${MIAOW_EXAMPLE_NAME}")
        else()
            message(FATAL_ERROR "Either EXAMPLE_NAME or EXECUTABLE_TARGET is required")
        endif()
    endif()

    if(NOT MIAOW_MODEL_TARGET)
        if(MIAOW_EXAMPLE_NAME)
            set(MIAOW_MODEL_TARGET "miaow_${MIAOW_EXAMPLE_NAME}_model")
        else()
            set(MIAOW_MODEL_TARGET "${MIAOW_EXECUTABLE_TARGET}_model")
        endif()
    endif()

    if(COMMAND miaow_resolve_model_root)
        miaow_resolve_model_root(model_root
            MODEL_ROOT "${MIAOW_MODEL_ROOT}"
            MODEL_LOCAL_SUBDIR "${MIAOW_MODEL_LOCAL_SUBDIR}"
            MODEL_LABEL "${MIAOW_MODEL_TARGET}"
        )
    else()
        if(MIAOW_MODEL_ROOT)
            set(model_root "${MIAOW_MODEL_ROOT}")
        elseif(MIAOW_MODEL_LOCAL_SUBDIR)
            set(model_root "${MIAOW_MIAOW_ROOT}/third_party/${MIAOW_MODEL_LOCAL_SUBDIR}")
        endif()
    endif()

    if(NOT model_root)
        message(FATAL_ERROR "Missing model source: set MODEL_ROOT or MODEL_LOCAL_SUBDIR")
    endif()

    if(NOT EXISTS "${model_root}")
        if(NOT MIAOW_MODEL_GIT_REPOSITORY OR NOT MIAOW_MODEL_GIT_TAG)
            message(FATAL_ERROR "Missing model source: set MODEL_ROOT or MODEL_GIT_REPOSITORY + MODEL_GIT_TAG")
        endif()

        get_filename_component(model_parent "${model_root}" DIRECTORY)
        file(MAKE_DIRECTORY "${model_parent}")

        include(FetchContent)
        if(NOT MIAOW_MODEL_FETCH_NAME)
            set(MIAOW_MODEL_FETCH_NAME "${MIAOW_MODEL_TARGET}_source")
        endif()
        string(MAKE_C_IDENTIFIER "${MIAOW_MODEL_FETCH_NAME}" model_fetch_name)

        FetchContent_Populate(
            ${model_fetch_name}
            GIT_REPOSITORY "${MIAOW_MODEL_GIT_REPOSITORY}"
            GIT_TAG "${MIAOW_MODEL_GIT_TAG}"
            SOURCE_DIR "${model_root}"
            BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${model_fetch_name}-build"
        )
    endif()

    if(NOT MIAOW_OUTPUT_DIR)
        set(MIAOW_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}")
    endif()

    if(NOT TARGET miaow)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
        add_subdirectory("${MIAOW_MIAOW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/miaow-build")
    endif()

    set(model_sources "")
    foreach(model_source IN LISTS MIAOW_MODEL_SOURCES)
        if(IS_ABSOLUTE "${model_source}" OR model_source MATCHES "^\\$<")
            list(APPEND model_sources "${model_source}")
        else()
            list(APPEND model_sources "${model_root}/${model_source}")
        endif()
    endforeach()

    set(model_include_dirs "")
    foreach(model_include_dir IN LISTS MIAOW_MODEL_INCLUDE_DIRS)
        if(IS_ABSOLUTE "${model_include_dir}" OR model_include_dir MATCHES "^\\$<")
            list(APPEND model_include_dirs "${model_include_dir}")
        else()
            list(APPEND model_include_dirs "${model_root}/${model_include_dir}")
        endif()
    endforeach()

    if(MIAOW_USE_MIAOW_BMI_INCLUDE)
        list(APPEND model_include_dirs "${MIAOW_MIAOW_ROOT}/include/third_party/bmi")
    endif()

    add_library(${MIAOW_MODEL_TARGET} STATIC ${model_sources})
    target_compile_features(${MIAOW_MODEL_TARGET} PUBLIC cxx_std_20)
    target_include_directories(${MIAOW_MODEL_TARGET} PUBLIC ${model_include_dirs})

    if(MIAOW_MODEL_COMPILE_DEFINITIONS)
        target_compile_definitions(${MIAOW_MODEL_TARGET} PRIVATE ${MIAOW_MODEL_COMPILE_DEFINITIONS})
    endif()

    if(MIAOW_MODEL_LINK_LIBRARIES)
        target_link_libraries(${MIAOW_MODEL_TARGET} PUBLIC ${MIAOW_MODEL_LINK_LIBRARIES})
    endif()

    add_executable(${MIAOW_EXECUTABLE_TARGET} ${MIAOW_EXECUTABLE_SOURCES})
    target_compile_features(${MIAOW_EXECUTABLE_TARGET} PRIVATE cxx_std_20)
    target_link_libraries(${MIAOW_EXECUTABLE_TARGET} PRIVATE miaow ${MIAOW_MODEL_TARGET})
    set_target_properties(${MIAOW_EXECUTABLE_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${MIAOW_OUTPUT_DIR}"
    )

    set(MODEL_ROOT "${model_root}" PARENT_SCOPE)
    set(MIAOW_MODEL_ROOT_RESOLVED "${model_root}" PARENT_SCOPE)
    set(MIAOW_EXECUTABLE_TARGET_RESOLVED "${MIAOW_EXECUTABLE_TARGET}" PARENT_SCOPE)
    set(MIAOW_MODEL_TARGET_RESOLVED "${MIAOW_MODEL_TARGET}" PARENT_SCOPE)
endfunction()

# miaow_add_workflow_test(TEST_NAME <name> EXECUTABLE_TARGET <target>
#     [WORKING_DIRECTORY <dir>] [PASS_REGULAR_EXPRESSION <regex>]
#     [CLI_ARGS <arg>...])
#
# Register a CTest test for a workflow-style example executable. CLI_ARGS are
# forwarded directly to the executable, while WORKING_DIRECTORY and
# PASS_REGULAR_EXPRESSION are applied only when provided.
function(miaow_add_workflow_test)
    set(options)
    set(one_value_args
        TEST_NAME
        EXECUTABLE_TARGET
        WORKING_DIRECTORY
        PASS_REGULAR_EXPRESSION
    )
    set(multi_value_args CLI_ARGS)
    cmake_parse_arguments(MIAOW_TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_test(
        NAME ${MIAOW_TEST_TEST_NAME}
        COMMAND
            $<TARGET_FILE:${MIAOW_TEST_EXECUTABLE_TARGET}>
            ${MIAOW_TEST_CLI_ARGS}
    )

    set(test_properties "")
    if(MIAOW_TEST_WORKING_DIRECTORY)
        list(APPEND test_properties WORKING_DIRECTORY "${MIAOW_TEST_WORKING_DIRECTORY}")
    endif()
    if(MIAOW_TEST_PASS_REGULAR_EXPRESSION)
        list(APPEND test_properties PASS_REGULAR_EXPRESSION "${MIAOW_TEST_PASS_REGULAR_EXPRESSION}")
    endif()
    if(test_properties)
        set_tests_properties(${MIAOW_TEST_TEST_NAME} PROPERTIES ${test_properties})
    endif()
endfunction()
