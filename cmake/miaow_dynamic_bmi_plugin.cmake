include(CMakeParseArguments)

# miaow_add_dynamic_bmi_plugin(PLUGIN_TARGET <target> SOURCES <src>...
#     [OUTPUT_NAME <file-name>] [FORTRAN_MODULE_DIR <dir>]
#     [MIAOW_ROOT <path>] [INCLUDE_DIRS <dir>...]
#     [COMPILE_DEFINITIONS <def>...] [COMPILE_OPTIONS <opt>...]
#     [LINK_LIBRARIES <lib>...] [LINK_ISO_C_BMI]
#     [USE_MIAOW_BMI_INCLUDE])
#
# Build a dynamic BMI plugin as a shared library.
#
# Required arguments:
#   - PLUGIN_TARGET: CMake target name for the shared library
#   - SOURCES: source files compiled into the plugin
#
# Optional arguments:
#   - OUTPUT_NAME: override the generated library file name
#   - FORTRAN_MODULE_DIR: directory for generated Fortran `.mod` files
#   - INCLUDE_DIRS / COMPILE_DEFINITIONS / COMPILE_OPTIONS / LINK_LIBRARIES:
#     passed through to the plugin target
#   - LINK_ISO_C_BMI: link the NOAA-OWP iso_c_bmi bridge library
#   - USE_MIAOW_BMI_INCLUDE: add miaow's vendored BMI C++ headers
#
# C++ plugins export `bmi_model_create` and `bmi_model_destroy` for a concrete
# `bmi::Bmi` implementation. Fortran plugins provide `register_bmi(void**)`,
# while the linked iso_c_bmi library supplies procedural BMI entry points such as
# `initialize` and `finalize`.
function(miaow_add_dynamic_bmi_plugin)
    set(options LINK_ISO_C_BMI USE_MIAOW_BMI_INCLUDE)
    set(one_value_args
        PLUGIN_TARGET
        OUTPUT_NAME
        FORTRAN_MODULE_DIR
        MIAOW_ROOT
    )
    set(multi_value_args
        SOURCES
        INCLUDE_DIRS
        COMPILE_DEFINITIONS
        COMPILE_OPTIONS
        LINK_LIBRARIES
    )
    cmake_parse_arguments(MIAOW_PLUGIN
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MIAOW_PLUGIN_PLUGIN_TARGET)
        message(FATAL_ERROR
            "miaow_add_dynamic_bmi_plugin: PLUGIN_TARGET is required")
    endif()
    if(NOT MIAOW_PLUGIN_SOURCES)
        message(FATAL_ERROR
            "miaow_add_dynamic_bmi_plugin: SOURCES is required")
    endif()

    if(NOT MIAOW_PLUGIN_MIAOW_ROOT)
        get_filename_component(MIAOW_PLUGIN_MIAOW_ROOT
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    endif()

    add_library(${MIAOW_PLUGIN_PLUGIN_TARGET} SHARED ${MIAOW_PLUGIN_SOURCES})
    target_compile_features(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE cxx_std_20)

    set(plugin_include_dirs ${MIAOW_PLUGIN_INCLUDE_DIRS})
    if(MIAOW_PLUGIN_USE_MIAOW_BMI_INCLUDE)
        list(APPEND plugin_include_dirs
            "${MIAOW_PLUGIN_MIAOW_ROOT}/include/third_party/bmi")
    endif()
    if(plugin_include_dirs)
        target_include_directories(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE
            ${plugin_include_dirs})
    endif()

    if(MIAOW_PLUGIN_COMPILE_DEFINITIONS)
        target_compile_definitions(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE
            ${MIAOW_PLUGIN_COMPILE_DEFINITIONS})
    endif()

    if(MIAOW_PLUGIN_COMPILE_OPTIONS)
        target_compile_options(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE
            ${MIAOW_PLUGIN_COMPILE_OPTIONS})
    endif()

    set(plugin_link_libs)
    if(MIAOW_PLUGIN_LINK_ISO_C_BMI)
        # Fortran dynamic plugins provide the factory entry point in their own
        # sources and delegate the procedural BMI surface to iso_c_bmi
        list(APPEND plugin_link_libs iso_c_bmi)
    endif()
    if(MIAOW_PLUGIN_LINK_LIBRARIES)
        list(APPEND plugin_link_libs ${MIAOW_PLUGIN_LINK_LIBRARIES})
    endif()
    if(plugin_link_libs)
        target_link_libraries(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE
            ${plugin_link_libs})
    endif()

    set(plugin_properties)
    if(MIAOW_PLUGIN_FORTRAN_MODULE_DIR)
        list(APPEND plugin_properties
            Fortran_MODULE_DIRECTORY "${MIAOW_PLUGIN_FORTRAN_MODULE_DIR}")
    endif()
    if(MIAOW_PLUGIN_OUTPUT_NAME)
        list(APPEND plugin_properties
            OUTPUT_NAME "${MIAOW_PLUGIN_OUTPUT_NAME}")
    endif()
    if(plugin_properties)
        set_target_properties(${MIAOW_PLUGIN_PLUGIN_TARGET} PROPERTIES
            ${plugin_properties})
    endif()
endfunction()

# miaow_attach_plugin_paths(<executable-target> <definition> <plugin-target> ...)
#
# Attach built plugin paths to an executable via compile definitions. Each
# definition receives the resolved $<TARGET_FILE:...> of its plugin target. The
# executable also depends on each plugin target so the paths are valid when the
# example or test is run.
function(miaow_attach_plugin_paths executable_target)
    list(LENGTH ARGN pair_args_length)
    math(EXPR pair_remainder "${pair_args_length} % 2")
    if(pair_args_length EQUAL 0 OR NOT pair_remainder EQUAL 0)
        message(FATAL_ERROR
            "miaow_attach_plugin_paths: expected one or more "
            "DEFINITION/PLUGIN_TARGET pairs after the executable target")
    endif()

    set(plugin_definitions)
    set(plugin_dependencies)

    set(pair_index 0)
    while(pair_index LESS pair_args_length)
        list(GET ARGN ${pair_index} definition_name)
        math(EXPR plugin_index "${pair_index} + 1")
        list(GET ARGN ${plugin_index} plugin_target)

        list(APPEND plugin_definitions
            "${definition_name}=\"$<TARGET_FILE:${plugin_target}>\"")
        list(APPEND plugin_dependencies "${plugin_target}")

        math(EXPR pair_index "${pair_index} + 2")
    endwhile()

    target_compile_definitions(${executable_target} PRIVATE
        ${plugin_definitions})
    add_dependencies(${executable_target} ${plugin_dependencies})
endfunction()
