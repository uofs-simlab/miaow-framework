# This script runs before compilation on every build so a checkout, commit, or tag
# change is reflected even when CMake has not been configured again.
# The generated header embeds the version in the library; running a job needs no Git.

# Without usable Git metadata, only the configured numeric version is known
set(MIAOW_BUILD_VERSION "${MIAOW_PROJECT_VERSION}")

# A .git directory or worktree file must belong to the MIAOW source root. Otherwise,
# Git could report an enclosing project's revision for an unpacked source archive.
if(MIAOW_GIT_EXECUTABLE AND EXISTS "${MIAOW_SOURCE_DIR}/.git")
    # --tags includes lightweight and annotated tags, restricted to version tags.
    # The description retains the tag, commit distance, and abbreviated hash.
    # --always falls back to a short hash when no version tag is reachable.
    # --dirty appends a suffix when tracked files have uncommitted changes.
    execute_process(
        COMMAND "${MIAOW_GIT_EXECUTABLE}" describe --tags --dirty --always
            --match "v[0-9]*"
        WORKING_DIRECTORY "${MIAOW_SOURCE_DIR}"
        RESULT_VARIABLE miaow_git_result
        OUTPUT_VARIABLE miaow_git_description
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(miaow_git_result STREQUAL "0")
        # An exact clean tag remains unchanged; other checkouts retain Git's suffixes
        set(MIAOW_BUILD_VERSION "${miaow_git_description}")
    endif()
endif()

# configure_file creates the output directory and only rewrites changed contents.
# Unchanged metadata therefore does not trigger recompilation of its consumers.
configure_file("${CMAKE_CURRENT_LIST_DIR}/miaow_build_info.hpp.in"
    "${MIAOW_OUTPUT_FILE}" @ONLY)
