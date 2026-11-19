# Applies PATCH to the source tree in SOURCE_DIR unless it is already applied.
#   cmake -DPATCH=<file> -DSOURCE_DIR=<dir> -P ApplyPatchOnce.cmake
execute_process (COMMAND git apply --check --ignore-whitespace "${PATCH}"
                 WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE canApply OUTPUT_QUIET ERROR_QUIET)
if (canApply EQUAL 0)
    execute_process (COMMAND git apply --ignore-whitespace "${PATCH}"
                     WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE applied)
    if (NOT applied EQUAL 0)
        message (FATAL_ERROR "Failed to apply ${PATCH}")
    endif()
    message (STATUS "Applied ${PATCH}")
else()
    execute_process (COMMAND git apply --reverse --check --ignore-whitespace "${PATCH}"
                     WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE alreadyApplied OUTPUT_QUIET ERROR_QUIET)
    if (alreadyApplied EQUAL 0)
        message (STATUS "Already applied: ${PATCH}")
    else()
        message (FATAL_ERROR "${PATCH} does not apply to ${SOURCE_DIR} (different JUCE version?)")
    endif()
endif()
