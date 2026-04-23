if(NOT DEFINED LINK_PATH OR LINK_PATH STREQUAL "")
    message(FATAL_ERROR "LINK_PATH is required")
endif()

if(NOT IS_SYMLINK "${LINK_PATH}")
    message(FATAL_ERROR "Expected symlink at ${LINK_PATH}")
endif()

if(DEFINED EXPECT_TARGET AND NOT EXPECT_TARGET STREQUAL "")
    file(READ_SYMLINK "${LINK_PATH}" link_target)

    if(IS_ABSOLUTE "${link_target}")
        set(link_target_abs "${link_target}")
    else()
        get_filename_component(link_path_parent "${LINK_PATH}" DIRECTORY)
        get_filename_component(link_target_abs
            "${link_path_parent}/${link_target}" ABSOLUTE)
    endif()

    get_filename_component(expect_target_abs "${EXPECT_TARGET}" ABSOLUTE)

    if(NOT link_target_abs STREQUAL expect_target_abs)
        message(FATAL_ERROR
            "Symlink target mismatch for ${LINK_PATH}: "
            "expected ${expect_target_abs}, got ${link_target_abs}")
    endif()
endif()
