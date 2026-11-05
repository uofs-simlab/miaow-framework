# Load helpers for creating plugins and passing their paths to the executable
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")

# Build the FUSE BMI shared library plugin
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fuse_plugin.cmake")

# Keep the data download opt-in and prefer an explicitly supplied dataset
set(MIAOW_FUSE_DATA_ROOT "" CACHE PATH
    "Path to an extracted FUSE grid dataset")
option(MIAOW_FUSE_FETCH_DATA
    "Download the FUSE grid dataset and prepare the Saltelli ensemble" OFF)

set(fuse_grid_archive_url
    "https://dl.dropboxusercontent.com/s/g5193e0n01ao33d/fuse_grid.zip?dl=0")
set(fuse_grid_archive_sha256
    "96b933943e653a085b792be77c97854a9e04d1747fd60b12f62e882a36416f23")
set(fuse_grid_runtime_dir "${CMAKE_BINARY_DIR}/fuse_grid_data")
set(fuse_grid_source_dir "")
set(fuse_paramset_dir "${CMAKE_CURRENT_LIST_DIR}/data/paramsets")

# The complete Saltelli ensemble is small enough to keep as repository data
file(GLOB fuse_paramset_files CONFIGURE_DEPENDS
    "${fuse_paramset_dir}/paramset_*.nc")
list(LENGTH fuse_paramset_files fuse_paramset_count)
if(NOT fuse_paramset_count EQUAL 256)
    message(FATAL_ERROR
        "FUSE example requires 256 parameter sets under ${fuse_paramset_dir}; "
        "found ${fuse_paramset_count}")
endif()

# Resolve an explicit dataset before the conventional ignored local directory
if(MIAOW_FUSE_DATA_ROOT)
    get_filename_component(fuse_grid_source_dir
        "${MIAOW_FUSE_DATA_ROOT}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
elseif(EXISTS "${MIAOW_DEFAULT_MODEL_BASE_DIR}/fuse_grid")
    set(fuse_grid_source_dir
        "${MIAOW_DEFAULT_MODEL_BASE_DIR}/fuse_grid")
elseif(MIAOW_FUSE_FETCH_DATA)
    # Use a stable user-facing directory instead of FetchContent's _deps path
    set(fuse_grid_download_dir "${fuse_grid_runtime_dir}/source")
    include(FetchContent)
    FetchContent_Populate(
        fuse_grid_archive
        URL "${fuse_grid_archive_url}"
        URL_HASH "SHA256=${fuse_grid_archive_sha256}"
        SOURCE_DIR "${fuse_grid_download_dir}"
        BINARY_DIR "${fuse_grid_runtime_dir}/download-build"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    # Accept either archive layout produced by supported CMake extractors
    if(EXISTS "${fuse_grid_download_dir}/fuse_grid/input")
        set(fuse_grid_source_dir
            "${fuse_grid_download_dir}/fuse_grid")
    else()
        set(fuse_grid_source_dir "${fuse_grid_download_dir}")
    endif()
endif()

set(fuse_grid_data_available FALSE)
if(fuse_grid_source_dir)
    # Fail during configuration with the exact missing dataset component
    set(fuse_grid_required_files
        input/cesm1-cam5_input.nc
        input/cesm1-cam5_elev_bands.nc
        settings/fuse_zConstraints_snow.txt
        settings/fuse_zDecisions_902.txt
        settings/fuse_zNumerix.txt
        settings/input_info.txt
    )
    foreach(fuse_grid_required_file IN LISTS fuse_grid_required_files)
        if(NOT EXISTS
           "${fuse_grid_source_dir}/${fuse_grid_required_file}")
            message(FATAL_ERROR
                "FUSE grid dataset is missing ${fuse_grid_required_file} under "
                "${fuse_grid_source_dir}")
        endif()
    endforeach()
    set(fuse_grid_data_available TRUE)
    message(STATUS
        "MIAOW: using FUSE grid data at ${fuse_grid_source_dir}")
elseif(BUILD_TESTING)
    message(STATUS
        "MIAOW: FUSE grid smoke test disabled; set MIAOW_FUSE_DATA_ROOT or "
        "MIAOW_FUSE_FETCH_DATA=ON")
endif()

# Use manually generated source-tree configs unless CMake prepares the dataset
set(fuse_config_dir "${CMAKE_CURRENT_LIST_DIR}/data/configs")
if(fuse_grid_data_available)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(fuse_task_dir "${fuse_grid_runtime_dir}/tasks")
    set(fuse_config_dir "${fuse_grid_runtime_dir}/configs")
    set(fuse_smoke_task_dir "${fuse_grid_runtime_dir}/smoke_tasks")
    set(fuse_smoke_config_dir "${fuse_grid_runtime_dir}/smoke_configs")
    # Changing the selected external dataset produces a separate preparation stamp
    string(SHA256 fuse_preparation_key "${fuse_grid_source_dir}")
    set(fuse_preparation_stamp
        "${fuse_grid_runtime_dir}/.prepared-${fuse_preparation_key}")

    # Generate machine-specific configs plus an isolated one-member smoke setup
    add_custom_command(
        OUTPUT "${fuse_preparation_stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
                "${fuse_task_dir}"
                "${fuse_config_dir}"
                "${fuse_smoke_task_dir}"
                "${fuse_smoke_config_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${fuse_task_dir}"
                "${fuse_config_dir}"
                "${fuse_smoke_task_dir}"
                "${fuse_smoke_config_dir}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/generate_configs.py"
                --paramset-dir "${fuse_paramset_dir}"
                --outdir "${fuse_config_dir}"
                --task-root "${fuse_task_dir}"
                --file-manager-template
                "${CMAKE_CURRENT_LIST_DIR}/cmake/fm_grid.txt.in"
                --settings-dir "${fuse_grid_source_dir}/settings"
                --input-dir "${fuse_grid_source_dir}/input"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/generate_configs.py"
                --count 1
                --paramset-dir "${fuse_paramset_dir}"
                --outdir "${fuse_smoke_config_dir}"
                --task-root "${fuse_smoke_task_dir}"
                --file-manager-template
                "${CMAKE_CURRENT_LIST_DIR}/cmake/fm_grid.txt.in"
                --settings-dir "${fuse_grid_source_dir}/settings"
                --input-dir "${fuse_grid_source_dir}/input"
                --output-mode nooutput
        COMMAND ${CMAKE_COMMAND} -E touch "${fuse_preparation_stamp}"
        DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/generate_configs.py"
            "${CMAKE_CURRENT_LIST_DIR}/cmake/fm_grid.txt.in"
            ${fuse_paramset_files}
        COMMENT "Preparing the FUSE Saltelli task configurations"
        VERBATIM
    )
    add_custom_target(miaow_fuse_prepare_data
        DEPENDS "${fuse_preparation_stamp}")
endif()

# Build the full Saltelli ensemble driver
add_executable(miaow_fuse_bmi main.cpp)
target_compile_features(miaow_fuse_bmi PRIVATE cxx_std_20)
target_link_libraries(miaow_fuse_bmi PRIVATE miaow)
target_compile_definitions(miaow_fuse_bmi PRIVATE
    MIAOW_FUSE_CONFIG_DIR="${fuse_config_dir}"
)
miaow_attach_plugin_paths(miaow_fuse_bmi
    MIAOW_FUSE_PLUGIN_PATH miaow_dynamic_fuse_plugin
)
add_dependencies(miaow_fuse_bmi miaow_subprocess)
if(fuse_grid_data_available)
    add_dependencies(miaow_fuse_bmi miaow_fuse_prepare_data)
endif()

# Place the binary in the example directory for the documented run command
set_target_properties(miaow_fuse_bmi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING AND fuse_grid_data_available)
    # Compile the same driver against only the first tracked parameter set
    add_executable(miaow_fuse_bmi_smoke main.cpp)
    target_compile_features(miaow_fuse_bmi_smoke PRIVATE cxx_std_20)
    target_link_libraries(miaow_fuse_bmi_smoke PRIVATE miaow)
    target_compile_definitions(miaow_fuse_bmi_smoke PRIVATE
        MIAOW_FUSE_CONFIG_DIR="${fuse_smoke_config_dir}"
    )
    miaow_attach_plugin_paths(miaow_fuse_bmi_smoke
        MIAOW_FUSE_PLUGIN_PATH miaow_dynamic_fuse_plugin
    )
    add_dependencies(miaow_fuse_bmi_smoke
        miaow_fuse_prepare_data
        miaow_subprocess
    )

    add_test(
        NAME miaow_example_06_fuse_bmi
        COMMAND $<TARGET_FILE:miaow_fuse_bmi_smoke>
            --max_workers 1
            --task_request_timeout 900
            --job_request_timeout 1200
    )
    set_tests_properties(miaow_example_06_fuse_bmi PROPERTIES
        LABELS "example;optional;external_model;subprocess"
        TIMEOUT 1200
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
