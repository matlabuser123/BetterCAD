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
