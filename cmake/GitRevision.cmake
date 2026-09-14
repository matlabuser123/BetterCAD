# Script mode (cmake -P): writes the current git revision into a C++ source.
#
# Runs on every build. configure_file() only rewrites OUTPUT_FILE when its
# content changes, so an unchanged revision triggers no recompilation.
#
# Inputs: SOURCE_DIR, TEMPLATE, OUTPUT_FILE
foreach(required SOURCE_DIR TEMPLATE OUTPUT_FILE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "GitRevision.cmake: ${required} is not set")
    endif()
endforeach()

set(BETTERCAD_GIT_REVISION "unknown")
set(BETTERCAD_GIT_DIRTY "false")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE revision
        RESULT_VARIABLE revision_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(revision_result EQUAL 0 AND revision)
        set(BETTERCAD_GIT_REVISION "${revision}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE status
            RESULT_VARIABLE status_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(status_result EQUAL 0 AND NOT status STREQUAL "")
            set(BETTERCAD_GIT_DIRTY "true")
        endif()
    endif()
endif()

configure_file("${TEMPLATE}" "${OUTPUT_FILE}" @ONLY)
