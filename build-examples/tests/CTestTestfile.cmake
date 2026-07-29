# CMake generated Testfile for 
# Source directory: /Users/tristanmontoya/Research/miaow-framework/tests
# Build directory: /Users/tristanmontoya/Research/miaow-framework/build-examples/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[miaow_core]=] "/Users/tristanmontoya/Research/miaow-framework/build-examples/tests/miaow_core_tests")
set_tests_properties([=[miaow_core]=] PROPERTIES  LABELS "framework" TIMEOUT "10" _BACKTRACE_TRIPLES "/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;44;add_test;/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;0;")
add_test([=[miaow_bmi]=] "/Users/tristanmontoya/Research/miaow-framework/build-examples/tests/miaow_bmi_tests")
set_tests_properties([=[miaow_bmi]=] PROPERTIES  LABELS "framework;subprocess" TIMEOUT "10" _BACKTRACE_TRIPLES "/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;45;add_test;/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;0;")
add_test([=[miaow_io]=] "/Users/tristanmontoya/Research/miaow-framework/build-examples/tests/miaow_io_tests")
set_tests_properties([=[miaow_io]=] PROPERTIES  LABELS "framework" TIMEOUT "10" _BACKTRACE_TRIPLES "/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;46;add_test;/Users/tristanmontoya/Research/miaow-framework/tests/CMakeLists.txt;0;")
