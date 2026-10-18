# Compare controller policies using the same in-process objective
add_executable(miaow_particle_swarm main.cpp)
target_compile_features(miaow_particle_swarm PRIVATE cxx_std_20)
target_link_libraries(miaow_particle_swarm PRIVATE miaow)
target_compile_definitions(miaow_particle_swarm PRIVATE
    MIAOW_PARTICLE_SWARM_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)
set_target_properties(miaow_particle_swarm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_08_particle_swarm
        EXECUTABLE_TARGET miaow_particle_swarm
    )
endif()
