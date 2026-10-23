# Build the C++ and Fortran heat model plugins
include("${PROJECT_SOURCE_DIR}/cmake/miaow_heat_models.cmake")
miaow_add_cpp_heat_model()
miaow_add_fortran_heat_model()

# Build the driver that submits tasks using both plugins
add_executable(miaow_dynamic_bmi
    main.cpp
)

target_compile_features(miaow_dynamic_bmi PRIVATE cxx_std_20)
target_link_libraries(miaow_dynamic_bmi PRIVATE miaow nlohmann_json::nlohmann_json)
add_dependencies(miaow_dynamic_bmi miaow_subprocess)
target_compile_definitions(miaow_dynamic_bmi PRIVATE
    MIAOW_DYNAMIC_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Compile the final plugin filenames into the driver and add build dependencies
miaow_attach_plugin_paths(miaow_dynamic_bmi
    MIAOW_CPP_HEAT_PLUGIN_PATH      miaow_dynamic_cpp_heat_plugin
    MIAOW_FORTRAN_HEAT_PLUGIN_PATH  miaow_dynamic_fortran_heat_plugin
)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_dynamic_bmi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_test(
        TEST_NAME miaow_example_03_dynamic_bmi
        EXECUTABLE_TARGET miaow_dynamic_bmi
        REGRESSION dynamic_bmi
        EXTERNAL_MODEL
    )
    foreach(scenario missing_cpp_plugin missing_fortran_plugin invalid_tolerance)
        miaow_add_example_test(
            TEST_NAME miaow_example_03_dynamic_bmi_${scenario}
            EXECUTABLE_TARGET miaow_dynamic_bmi
            REGRESSION failure
            CASE_FILE "${PROJECT_SOURCE_DIR}/tests/cases/dynamic_${scenario}.json"
            EXTERNAL_MODEL
        )
    endforeach()
endif()
