cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(libtrixi_bmi)

set(MIAOW_LIBTRIXI_BMI_ROOT "" CACHE PATH
    "Path to a local tristanmontoya/libtrixi-bmi source tree")
set(libtrixi_config_default "${CMAKE_CURRENT_LIST_DIR}/advection_basic_1d.jl")
set(LIBTRIXI_CONFIG "${libtrixi_config_default}" CACHE FILEPATH
    "Default libtrixi config run by the example")
if(NOT LIBTRIXI_CONFIG)
    set(LIBTRIXI_CONFIG "${libtrixi_config_default}" CACHE FILEPATH
        "Default libtrixi config run by the example" FORCE)
endif()

if(NOT EXISTS "${LIBTRIXI_CONFIG}")
    message(FATAL_ERROR "LIBTRIXI_CONFIG must point to a libtrixi config")
endif()

# Resolve/fetch the libtrixi BMI plugin source tree
miaow_resolve_model_root(libtrixi_bmi_root
    MODEL_ROOT "${MIAOW_LIBTRIXI_BMI_ROOT}"
    MODEL_LOCAL_SUBDIR "libtrixi-bmi"
    MODEL_LABEL "libtrixi_bmi"
)

if(NOT EXISTS "${libtrixi_bmi_root}")
    include(FetchContent)
    FetchContent_Populate(
        libtrixi_bmi_source
        GIT_REPOSITORY https://github.com/tristanmontoya/libtrixi-bmi.git
        GIT_TAG "42195870543eff74a986b8b8c7be42c731557b30"
        SOURCE_DIR "${libtrixi_bmi_root}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/libtrixi_bmi_source-build"
    )
endif()

if(NOT EXISTS "${libtrixi_bmi_root}/CMakeLists.txt")
    message(FATAL_ERROR
        "MIAOW_LIBTRIXI_BMI_ROOT must point to a tristanmontoya/libtrixi-bmi checkout")
endif()

set(libtrixi_install_dir_default "${libtrixi_bmi_root}/build/libtrixi-install")
set(libtrixi_project_dir_default "${libtrixi_bmi_root}/build/libtrixi-julia")
set(libtrixi_julia_depot_path_default "${libtrixi_bmi_root}/build/julia-depot")
set(LIBTRIXI_INSTALL_DIR "${libtrixi_install_dir_default}" CACHE PATH
    "Directory where libtrixi is installed")
if(NOT LIBTRIXI_INSTALL_DIR)
    set(LIBTRIXI_INSTALL_DIR "${libtrixi_install_dir_default}" CACHE PATH
        "Directory where libtrixi is installed" FORCE)
endif()
set(LIBTRIXI_PROJECT_DIR "${libtrixi_project_dir_default}" CACHE PATH
    "Existing libtrixi Julia project directory passed to trixi_initialize")
if(NOT LIBTRIXI_PROJECT_DIR)
    set(LIBTRIXI_PROJECT_DIR "${libtrixi_project_dir_default}" CACHE PATH
        "Existing libtrixi Julia project directory passed to trixi_initialize" FORCE)
endif()
set(LIBTRIXI_JULIA_DEPOT_PATH "${libtrixi_julia_depot_path_default}" CACHE PATH
    "Optional Julia depot path used as a JULIA_DEPOT_PATH fallback")
if(NOT LIBTRIXI_JULIA_DEPOT_PATH)
    set(LIBTRIXI_JULIA_DEPOT_PATH "${libtrixi_julia_depot_path_default}" CACHE PATH
        "Optional Julia depot path used as a JULIA_DEPOT_PATH fallback" FORCE)
endif()
if(NOT EXISTS "${LIBTRIXI_INSTALL_DIR}/include/trixi.h")
    message(FATAL_ERROR
        "LIBTRIXI_INSTALL_DIR must point to the directory where libtrixi is "
        "installed. Default: ${libtrixi_install_dir_default}")
endif()
if(NOT EXISTS "${LIBTRIXI_PROJECT_DIR}/Project.toml")
    message(FATAL_ERROR
        "LIBTRIXI_PROJECT_DIR must contain a Julia Project.toml. "
        "Default: ${libtrixi_project_dir_default}")
endif()
set(LIBTRIXI_PREFIX "${LIBTRIXI_INSTALL_DIR}" CACHE PATH
    "Installed libtrixi prefix used by libtrixi-bmi" FORCE)

add_subdirectory(
    "${libtrixi_bmi_root}"
    "${CMAKE_CURRENT_BINARY_DIR}/libtrixi-bmi-build"
)

miaow_provision_example_extern_link(
    EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    LINK_NAME "libtrixi-bmi"
    TARGET_DIR "${libtrixi_bmi_root}"
)

add_executable(${EXECUTABLE_TARGET}
    main.cpp
)
target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_LIBTRIXI_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
    LIBTRIXI_BMI_PLUGIN_PATH="$<TARGET_FILE:libtrixi_bmi::trixi_bmi>"
    LIBTRIXI_PROJECT_DIR="${LIBTRIXI_PROJECT_DIR}"
    LIBTRIXI_JULIA_DEPOT_PATH="${LIBTRIXI_JULIA_DEPOT_PATH}"
    LIBTRIXI_CONFIG="${LIBTRIXI_CONFIG}"
)
add_dependencies(${EXECUTABLE_TARGET} miaow_subprocess libtrixi_bmi_plugin)

# Drop the binary next to the source so example paths are easy to run manually
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
