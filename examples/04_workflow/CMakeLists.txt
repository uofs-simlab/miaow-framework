add_executable(miaow_workflow main.cpp)
target_compile_features(miaow_workflow PRIVATE cxx_std_20)
target_link_libraries(miaow_workflow PRIVATE miaow)
set_target_properties(miaow_workflow PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    add_executable(miaow_workflow_checks
        "${PROJECT_SOURCE_DIR}/tests/workflow_record_checks.cpp"
    )
    target_compile_features(miaow_workflow_checks PRIVATE cxx_std_20)
    target_link_libraries(miaow_workflow_checks PRIVATE
        miaow nlohmann_json::nlohmann_json Boost::unit_test_framework)
    target_include_directories(miaow_workflow_checks PRIVATE
        "${PROJECT_BINARY_DIR}/generated")
    add_dependencies(miaow_workflow_checks miaow_build_info)
    add_dependencies(miaow_workflow miaow_workflow_checks)

    miaow_add_example_test(
        TEST_NAME miaow_example_04_workflow
        EXECUTABLE_TARGET miaow_workflow
        REGRESSION workflow
    )
endif()
