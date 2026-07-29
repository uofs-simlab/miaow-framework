# CMake generated Testfile for 
# Source directory: /Users/tristanmontoya/Research/miaow-framework/examples/01_heat
# Build directory: /Users/tristanmontoya/Research/miaow-framework/build-examples/examples/01_heat
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[miaow_example_01_heat]=] "/Users/tristanmontoya/Research/miaow-framework/examples/01_heat/miaow_heat")
set_tests_properties([=[miaow_example_01_heat]=] PROPERTIES  LABELS "example;external_model" TIMEOUT "360" WORKING_DIRECTORY "/Users/tristanmontoya/Research/miaow-framework/examples/01_heat" _BACKTRACE_TRIPLES "/Users/tristanmontoya/Research/miaow-framework/cmake/miaow_setup_example.cmake;90;add_test;/Users/tristanmontoya/Research/miaow-framework/examples/01_heat/CMakeLists.txt;40;miaow_add_example_smoke_test;/Users/tristanmontoya/Research/miaow-framework/examples/01_heat/CMakeLists.txt;0;")
