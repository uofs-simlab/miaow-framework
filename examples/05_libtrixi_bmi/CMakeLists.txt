# Allow a local checkout to override the default third_party source
set(MIAOW_LIBTRIXI_BMI_ROOT "" CACHE PATH
    "Path to a local tristanmontoya/libtrixi-bmi source tree")

# Choose the plugin source path, fetching the pinned source when absent
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

# Validate the selected source tree before reading its build configuration
if(NOT EXISTS "${libtrixi_bmi_root}/CMakeLists.txt")
    message(FATAL_ERROR
        "MIAOW_LIBTRIXI_BMI_ROOT must point to a tristanmontoya/libtrixi-bmi checkout")
endif()

# Locate the libtrixi installation and Julia environment used by the plugin
set(libtrixi_install_dir_default "${libtrixi_bmi_root}/build/libtrixi-install")
set(libtrixi_project_dir_default "${libtrixi_bmi_root}/build/libtrixi-julia")
set(libtrixi_julia_depot_path_default "${libtrixi_bmi_root}/build/julia-depot")
set(LIBTRIXI_INSTALL_DIR "${libtrixi_install_dir_default}" CACHE PATH
    "Directory where libtrixi is installed")
set(LIBTRIXI_PROJECT_DIR "${libtrixi_project_dir_default}" CACHE PATH
    "Existing libtrixi Julia project directory passed to trixi_initialize")
set(LIBTRIXI_DEPOT_PATH "${libtrixi_julia_depot_path_default}" CACHE PATH
    "Optional Julia depot path passed to trixi_initialize")
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

# Let libtrixi-bmi define its plugin target in a separate binary directory
add_subdirectory(
    "${libtrixi_bmi_root}"
    "${CMAKE_CURRENT_BINARY_DIR}/libtrixi-bmi-build"
)

# Build the driver and embed the plugin and Julia environment paths
add_executable(miaow_libtrixi_bmi
    main.cpp
)
target_compile_features(miaow_libtrixi_bmi PRIVATE cxx_std_20)
target_link_libraries(miaow_libtrixi_bmi PRIVATE miaow)
target_compile_definitions(miaow_libtrixi_bmi PRIVATE
    MIAOW_LIBTRIXI_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
    LIBTRIXI_BMI_PLUGIN_PATH="$<TARGET_FILE:libtrixi_bmi::trixi_bmi>"
    LIBTRIXI_PROJECT_DIR="${LIBTRIXI_PROJECT_DIR}"
    LIBTRIXI_DEPOT_PATH="${LIBTRIXI_DEPOT_PATH}"
)
add_dependencies(miaow_libtrixi_bmi miaow_subprocess libtrixi_bmi_plugin)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_libtrixi_bmi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_05_libtrixi_bmi
        EXECUTABLE_TARGET miaow_libtrixi_bmi
        EXTERNAL_MODEL
    )
endif()
