include(CMakeParseArguments)

# miaow_add_bmi_model(EXECUTABLE_TARGET <target>
#     [MODEL_ROOT <dir> | MODEL_LOCAL_SUBDIR <dir>]
#     [MODEL_GIT_REPOSITORY <url> MODEL_GIT_TAG <rev>]
#     [EXECUTABLE_SOURCES <src>...] [MODEL_SOURCES <src>...]
#     [MODEL_INCLUDE_DIRS <dir>...]
#     [MODEL_COMPILE_DEFINITIONS <def>...]
#     [USE_MIAOW_BMI_INCLUDE])
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
# Relative MODEL_SOURCES and MODEL_INCLUDE_DIRS use the resolved model root
function(miaow_add_bmi_model)
    set(options USE_MIAOW_BMI_INCLUDE)
    set(one_value_args
        MODEL_ROOT
        MODEL_LOCAL_SUBDIR
        MODEL_GIT_REPOSITORY
        MODEL_GIT_TAG
        EXECUTABLE_TARGET
    )
    set(multi_value_args
        EXECUTABLE_SOURCES
        MODEL_SOURCES
        MODEL_INCLUDE_DIRS
        MODEL_COMPILE_DEFINITIONS
    )
    cmake_parse_arguments(MIAOW
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MIAOW_EXECUTABLE_TARGET)
        message(FATAL_ERROR
            "miaow_add_bmi_model: EXECUTABLE_TARGET is required")
    endif()
    set(model_target "${MIAOW_EXECUTABLE_TARGET}_model")

    # Select either the explicit checkout or the default model directory
    miaow_resolve_model_root(model_root
        MODEL_ROOT "${MIAOW_MODEL_ROOT}"
        MODEL_LOCAL_SUBDIR "${MIAOW_MODEL_LOCAL_SUBDIR}"
        MODEL_LABEL "${model_target}"
    )

    # Fetch the pinned model only when the selected source directory is absent
    if(NOT EXISTS "${model_root}")
        if(NOT MIAOW_MODEL_GIT_REPOSITORY OR NOT MIAOW_MODEL_GIT_TAG)
            message(FATAL_ERROR
                "Missing model source: set MODEL_ROOT or both "
                "MODEL_GIT_REPOSITORY and MODEL_GIT_TAG")
        endif()

        get_filename_component(model_parent "${model_root}" DIRECTORY)
        file(MAKE_DIRECTORY "${model_parent}")

        include(FetchContent)
        set(model_fetch_name "${model_target}_source")

        FetchContent_Populate(
            ${model_fetch_name}
            GIT_REPOSITORY "${MIAOW_MODEL_GIT_REPOSITORY}"
            GIT_TAG "${MIAOW_MODEL_GIT_TAG}"
            SOURCE_DIR "${model_root}"
            BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${model_fetch_name}-build"
        )
    endif()

    # Expand model-relative source paths into absolute paths
    set(model_sources "")
    foreach(model_source IN LISTS MIAOW_MODEL_SOURCES)
        if(IS_ABSOLUTE "${model_source}" OR model_source MATCHES "^\\$<")
            list(APPEND model_sources "${model_source}")
        else()
            list(APPEND model_sources "${model_root}/${model_source}")
        endif()
    endforeach()

    # Expand model-relative include paths into absolute paths
    set(model_include_dirs "")
    foreach(model_include_dir IN LISTS MIAOW_MODEL_INCLUDE_DIRS)
        if(IS_ABSOLUTE "${model_include_dir}" OR model_include_dir MATCHES "^\\$<")
            list(APPEND model_include_dirs "${model_include_dir}")
        else()
            list(APPEND model_include_dirs "${model_root}/${model_include_dir}")
        endif()
    endforeach()

    if(MIAOW_USE_MIAOW_BMI_INCLUDE)
        list(APPEND model_include_dirs "${MIAOW_ROOT}/include/third_party/bmi")
    endif()

    # Build the model library before linking it into the example driver
    add_library(${model_target} STATIC ${model_sources})
    target_compile_features(${model_target} PUBLIC cxx_std_20)
    target_include_directories(${model_target} PUBLIC ${model_include_dirs})

    if(MIAOW_MODEL_COMPILE_DEFINITIONS)
        target_compile_definitions(${model_target} PRIVATE
            ${MIAOW_MODEL_COMPILE_DEFINITIONS})
    endif()

    add_executable(${MIAOW_EXECUTABLE_TARGET} ${MIAOW_EXECUTABLE_SOURCES})
    target_compile_features(${MIAOW_EXECUTABLE_TARGET} PRIVATE cxx_std_20)
    target_link_libraries(${MIAOW_EXECUTABLE_TARGET} PRIVATE miaow ${model_target})
    set_target_properties(${MIAOW_EXECUTABLE_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endfunction()
