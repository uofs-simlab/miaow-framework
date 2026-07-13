include(CMakeParseArguments)

# miaow_add_dynamic_bmi_plugin(PLUGIN_TARGET <target> SOURCES <src>...
#     [FORTRAN_MODULE_DIR <dir>] [INCLUDE_DIRS <dir>...]
#     [LINK_ISO_C_BMI]
#     [USE_MIAOW_BMI_INCLUDE])
#
# Build a dynamic BMI plugin as a shared library.
#
# Required arguments:
#   - PLUGIN_TARGET: CMake target name for the shared library
#   - SOURCES: source files compiled into the plugin
#
# Optional arguments:
#   - FORTRAN_MODULE_DIR: directory for generated Fortran `.mod` files
#   - INCLUDE_DIRS: additional plugin header search paths
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
        FORTRAN_MODULE_DIR
    )
    set(multi_value_args
        SOURCES
        INCLUDE_DIRS
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

    # Create the shared library that the runtime will load by filename
    add_library(${MIAOW_PLUGIN_PLUGIN_TARGET} SHARED ${MIAOW_PLUGIN_SOURCES})
    target_compile_features(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE cxx_std_20)

    # Add caller-provided headers and the optional vendored BMI interface
    set(plugin_include_dirs ${MIAOW_PLUGIN_INCLUDE_DIRS})
    if(MIAOW_PLUGIN_USE_MIAOW_BMI_INCLUDE)
        get_filename_component(miaow_root
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
        list(APPEND plugin_include_dirs
            "${miaow_root}/include/third_party/bmi")
    endif()
    if(plugin_include_dirs)
        target_include_directories(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE
            ${plugin_include_dirs})
    endif()

    # Link the procedural bridge only for plugins that request it
    if(MIAOW_PLUGIN_LINK_ISO_C_BMI)
        # Fortran dynamic plugins provide the factory entry point in their own
        # sources and delegate the procedural BMI surface to iso_c_bmi
        target_link_libraries(${MIAOW_PLUGIN_PLUGIN_TARGET} PRIVATE iso_c_bmi)
    endif()

    # Keep generated Fortran modules in the caller-provided build directory
    if(MIAOW_PLUGIN_FORTRAN_MODULE_DIR)
        set_target_properties(${MIAOW_PLUGIN_PLUGIN_TARGET} PROPERTIES
            Fortran_MODULE_DIRECTORY "${MIAOW_PLUGIN_FORTRAN_MODULE_DIR}")
    endif()
endfunction()

# miaow_attach_plugin_paths(<executable-target> <definition> <plugin-target> ...)
#
# Attach built plugin paths to an executable via compile definitions. Each
# definition receives the resolved $<TARGET_FILE:...> of its plugin target. The
# executable also depends on each plugin target so the paths are valid when the
# example or test is run.
function(miaow_attach_plugin_paths executable_target)
    # Read each compile-definition and plugin-target pair in order
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

    # Embed resolved plugin paths and ensure the plugins build first
    target_compile_definitions(${executable_target} PRIVATE
        ${plugin_definitions})
    add_dependencies(${executable_target} ${plugin_dependencies})
endfunction()
