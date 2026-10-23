# Build one readable test executable for each MIAOW source module
add_executable(miaow_core_tests
    core/runtime.cpp
)
target_compile_features(miaow_core_tests PRIVATE cxx_std_20)
target_include_directories(miaow_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(miaow_core_tests PRIVATE
    MIAOW_CORE_TEST_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/output/core"
)
target_link_libraries(miaow_core_tests PRIVATE
    miaow nlohmann_json::nlohmann_json Boost::unit_test_framework)

add_executable(miaow_bmi_tests
    bmi/bmi.cpp
    bmi/variable.cpp
)
target_compile_features(miaow_bmi_tests PRIVATE cxx_std_20)
target_include_directories(miaow_bmi_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(miaow_bmi_tests PRIVATE
    MIAOW_BMI_TEST_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/output/bmi"
    MIAOW_TEST_BMI_FORCING_PATH="${CMAKE_CURRENT_SOURCE_DIR}/bmi/fixtures/forcing.csv"
)
target_link_libraries(miaow_bmi_tests PRIVATE
    miaow
    nlohmann_json::nlohmann_json
    Boost::unit_test_framework
)
add_dependencies(miaow_bmi_tests miaow_subprocess)

add_executable(miaow_io_tests
    io/io.cpp
    io/json.cpp
)
target_compile_features(miaow_io_tests PRIVATE cxx_std_20)
target_include_directories(miaow_io_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(miaow_io_tests PRIVATE
    MIAOW_IO_TEST_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/output/io"
)
target_link_libraries(miaow_io_tests PRIVATE
    miaow nlohmann_json::nlohmann_json Boost::unit_test_framework)

# Build the local Fortran BMI fixture used by the interoperability tests
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")
miaow_add_dynamic_bmi_plugin(
    PLUGIN_TARGET miaow_test_fortran_bmi
    SOURCES
        bmi/fixtures/simple_fortran_bmi.f90
    LINK_ISO_C_BMI
    FORTRAN_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fortran-mod"
)
miaow_attach_plugin_paths(
    miaow_bmi_tests
    MIAOW_TEST_FORTRAN_BMI_PLUGIN_PATH
    miaow_test_fortran_bmi
)

# Register module suites directly so CTest output stays compact
add_test(NAME miaow_core COMMAND miaow_core_tests)
add_test(NAME miaow_bmi COMMAND miaow_bmi_tests)
add_test(NAME miaow_io COMMAND miaow_io_tests)
set_tests_properties(miaow_core miaow_bmi miaow_io PROPERTIES
    LABELS framework
    TIMEOUT 10
)
set_property(TEST miaow_bmi APPEND PROPERTY LABELS subprocess)
