# Script mode (cmake -P): run a process and check its exit code and output.
#
# Inputs:
#   COMMAND               executable to run
#   ARGS                  arguments separated by '|' (optional)
#   EXPECTED_EXIT_CODE    required exit code
#   EXPECTED_STDOUT_REGEX regex that stdout must match (optional)
#   EXPECTED_STDERR_REGEX regex that stderr must match (optional)
#   EXPECTED_FILE         file the command must create (optional); it is
#                         deleted before the run, so a stale file cannot pass
#   EXPECTED_FILE_REGEX   regex that the file's first 256 bytes must match
#   COPY_FROM, COPY_TO    copy COPY_FROM to COPY_TO before running (optional)
#
# COPY_FROM/COPY_TO is what makes a test that EDITS a document repeatable. A
# command that edits a file in place is not idempotent -- the second run sees
# what the first one did -- so `ctest --repeat until-fail:N` would fail it on
# the second pass for a reason that has nothing to do with the code. Copying a
# pristine input first makes every run start from the same state.
if(NOT DEFINED COMMAND OR NOT DEFINED EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "RunAndCheck.cmake: COMMAND and EXPECTED_EXIT_CODE are required")
endif()

set(argv)
if(NOT "${ARGS}" STREQUAL "")
    string(REPLACE "|" ";" argv "${ARGS}")
endif()
if(NOT "${EXPECTED_FILE}" STREQUAL "")
    file(REMOVE "${EXPECTED_FILE}")
endif()
if(NOT "${COPY_FROM}" STREQUAL "")
    if(NOT EXISTS "${COPY_FROM}")
        message(FATAL_ERROR "RunAndCheck.cmake: COPY_FROM does not exist: ${COPY_FROM}")
    endif()
    file(COPY_FILE "${COPY_FROM}" "${COPY_TO}" RESULT copy_error)
    if(copy_error)
        message(FATAL_ERROR "RunAndCheck.cmake: cannot copy ${COPY_FROM} to ${COPY_TO}: ${copy_error}")
    endif()
endif()

execute_process(
    COMMAND "${COMMAND}" ${argv}
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 60
)

# Normalise line endings and strip surrounding whitespace so regexes can
# anchor with ^ and $ without embedding newlines in test command lines.
string(REPLACE "\r\n" "\n" stdout "${stdout}")
string(REPLACE "\r\n" "\n" stderr "${stderr}")
string(STRIP "${stdout}" stdout)
string(STRIP "${stderr}" stderr)

message(STATUS "command  : ${COMMAND} ${argv}")
message(STATUS "exit code: ${exit_code} (expected ${EXPECTED_EXIT_CODE})")
message(STATUS "stdout:\n${stdout}")
message(STATUS "stderr:\n${stderr}")

set(failures)
if(NOT "${exit_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
    list(APPEND failures "exit code ${exit_code} != expected ${EXPECTED_EXIT_CODE}")
endif()
if(NOT "${EXPECTED_STDOUT_REGEX}" STREQUAL "" AND NOT stdout MATCHES "${EXPECTED_STDOUT_REGEX}")
    list(APPEND failures "stdout does not match regex: ${EXPECTED_STDOUT_REGEX}")
endif()
if(NOT "${EXPECTED_STDERR_REGEX}" STREQUAL "" AND NOT stderr MATCHES "${EXPECTED_STDERR_REGEX}")
    list(APPEND failures "stderr does not match regex: ${EXPECTED_STDERR_REGEX}")
endif()
if(NOT "${EXPECTED_FILE}" STREQUAL "")
    if(NOT EXISTS "${EXPECTED_FILE}")
        list(APPEND failures "file was not created: ${EXPECTED_FILE}")
    elseif(NOT "${EXPECTED_FILE_REGEX}" STREQUAL "")
        file(READ "${EXPECTED_FILE}" head LIMIT 256)
        message(STATUS "file head:\n${head}")
        if(NOT head MATCHES "${EXPECTED_FILE_REGEX}")
            list(APPEND failures "file does not match regex: ${EXPECTED_FILE_REGEX}")
        endif()
    endif()
endif()

if(failures)
    list(JOIN failures "\n  " failure_text)
    message(FATAL_ERROR "Process check failed:\n  ${failure_text}")
endif()
