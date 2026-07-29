
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was miaowConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# Locate the Boost components used by the static MIAOW library
find_package(Boost CONFIG QUIET COMPONENTS process json)
if(NOT TARGET Boost::process OR NOT TARGET Boost::json)
    find_package(Boost QUIET COMPONENTS process json)
endif()
if(NOT TARGET Boost::process OR NOT TARGET Boost::json)
    set(miaow_FOUND FALSE)
    set(miaow_NOT_FOUND_MESSAGE
        "MIAOW requires Boost.Process and Boost.JSON targets")
    return()
endif()

# Normalize the CAF target variants supported by MIAOW
set(MIAOW_CAF_CORE_TARGET "")
set(MIAOW_CAF_IO_TARGET "")
find_package(CAF 1.1.0 QUIET COMPONENTS core io)
if(TARGET CAF::core AND TARGET CAF::io)
    set(MIAOW_CAF_CORE_TARGET CAF::core)
    set(MIAOW_CAF_IO_TARGET CAF::io)
elseif(TARGET caf::core AND TARGET caf::io)
    set(MIAOW_CAF_CORE_TARGET caf::core)
    set(MIAOW_CAF_IO_TARGET caf::io)
elseif(TARGET caf::caf_core AND TARGET caf::caf_io)
    set(MIAOW_CAF_CORE_TARGET caf::caf_core)
    set(MIAOW_CAF_IO_TARGET caf::caf_io)
endif()

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    find_package(caf 1.1.0 QUIET COMPONENTS core io)
    if(TARGET CAF::core AND TARGET CAF::io)
        set(MIAOW_CAF_CORE_TARGET CAF::core)
        set(MIAOW_CAF_IO_TARGET CAF::io)
    elseif(TARGET caf::core AND TARGET caf::io)
        set(MIAOW_CAF_CORE_TARGET caf::core)
        set(MIAOW_CAF_IO_TARGET caf::io)
    elseif(TARGET caf::caf_core AND TARGET caf::caf_io)
        set(MIAOW_CAF_CORE_TARGET caf::caf_core)
        set(MIAOW_CAF_IO_TARGET caf::caf_io)
    endif()
endif()

if(NOT MIAOW_CAF_CORE_TARGET OR NOT MIAOW_CAF_IO_TARGET)
    set(miaow_FOUND FALSE)
    set(miaow_NOT_FOUND_MESSAGE
        "MIAOW requires CAF 1.1.0 or newer with core and io targets")
    return()
endif()

# Load the sole exported target and attach its resolved dependencies
string(CONCAT MIAOW_ISO_C_BMI_LIBRARY
    "/usr/local/lib/"
    "libiso_c_bmi.dylib")
set_and_check(MIAOW_ISO_C_BMI_LIBRARY "${MIAOW_ISO_C_BMI_LIBRARY}")
include("${CMAKE_CURRENT_LIST_DIR}/miaowTargets.cmake")
set_property(TARGET miaow::miaow APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    "${MIAOW_CAF_CORE_TARGET}"
    "${MIAOW_CAF_IO_TARGET}"
    "$<LINK_ONLY:Boost::process>"
    "$<LINK_ONLY:Boost::json>"
    "$<LINK_ONLY:${MIAOW_ISO_C_BMI_LIBRARY}>"
)

unset(MIAOW_CAF_CORE_TARGET)
unset(MIAOW_CAF_IO_TARGET)
unset(MIAOW_ISO_C_BMI_LIBRARY)

check_required_components(miaow)
