add_executable(miaow_workflow main.cpp)
target_compile_features(miaow_workflow PRIVATE cxx_std_20)
target_link_libraries(miaow_workflow PRIVATE miaow)
set_target_properties(miaow_workflow PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
