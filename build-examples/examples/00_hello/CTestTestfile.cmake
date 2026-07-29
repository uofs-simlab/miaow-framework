# CMake generated Testfile for 
# Source directory: /Users/tristanmontoya/Research/miaow-framework/examples/00_hello
# Build directory: /Users/tristanmontoya/Research/miaow-framework/build-examples/examples/00_hello
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[miaow_example_00_hello]=] "/Users/tristanmontoya/Research/miaow-framework/examples/00_hello/miaow_hello")
set_tests_properties([=[miaow_example_00_hello]=] PROPERTIES  LABELS "example" TIMEOUT "360" WORKING_DIRECTORY "/Users/tristanmontoya/Research/miaow-framework/examples/00_hello" _BACKTRACE_TRIPLES "/Users/tristanmontoya/Research/miaow-framework/cmake/miaow_setup_example.cmake;90;add_test;/Users/tristanmontoya/Research/miaow-framework/examples/00_hello/CMakeLists.txt;29;miaow_add_example_smoke_test;/Users/tristanmontoya/Research/miaow-framework/examples/00_hello/CMakeLists.txt;0;")
