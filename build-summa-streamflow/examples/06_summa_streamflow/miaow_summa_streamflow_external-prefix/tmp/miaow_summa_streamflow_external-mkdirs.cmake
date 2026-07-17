# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/tristanmontoya/Projects/miaow-framework/third_party/summa/build")
  file(MAKE_DIRECTORY "/Users/tristanmontoya/Projects/miaow-framework/third_party/summa/build")
endif()
file(MAKE_DIRECTORY
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/summa-build"
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix"
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/tmp"
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/src/miaow_summa_streamflow_external-stamp"
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/src"
  "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/src/miaow_summa_streamflow_external-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/src/miaow_summa_streamflow_external-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/tristanmontoya/Projects/miaow-framework/build-summa-streamflow/examples/06_summa_streamflow/miaow_summa_streamflow_external-prefix/src/miaow_summa_streamflow_external-stamp${cfgdir}") # cfgdir has leading slash
endif()
