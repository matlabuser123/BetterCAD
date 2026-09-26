# Script mode (cmake -P): the failure paths of cmake/DeployQtRuntime.cmake.
#
# Deployment used to be trusted to windeployqt's exit code. It is not enough:
# in INFRA-QT-DEPLOY-001 windeployqt resolved the Qt binary directory through
# an ambiguous 8.3 short name, reported a path nobody had configured, and the
# build produced a GUI executable that could not start. The production script
# now also checks *what* was deployed and *where from*, and says so in terms a
# reader can act on. Those checks are driven here against a stand-in for
# windeployqt, so each one can be made to fire without a Qt installation in a
# particular state.
#
# Inputs: CASE, DEPLOY_SCRIPT, WORK_DIR
foreach(required CASE DEPLOY_SCRIPT WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "DeployFailurePaths.cmake: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(qt_bin_dir "${WORK_DIR}/qt/bin")
set(executable "${WORK_DIR}/app/bettercad.exe")
file(MAKE_DIRECTORY "${qt_bin_dir}")
file(MAKE_DIRECTORY "${WORK_DIR}/app")
file(WRITE "${executable}" "not a real executable")

# A stand-in for windeployqt: `lines` are echoed, `exit_code` returned, and
# `deploy` lists files it pretends to deploy next to the executable.
function(write_stub lines exit_code deploy out_var)
    set(stub "${WORK_DIR}/windeployqt-stub.cmd")
    set(text "@echo off\n")
    foreach(line IN LISTS lines)
        string(APPEND text "echo ${line}\n")
    endforeach()
    foreach(file IN LISTS deploy)
        file(TO_NATIVE_PATH "${WORK_DIR}/app/${file}" native)
        string(APPEND text "type nul > \"${native}\"\n")
    endforeach()
    string(APPEND text "exit /b ${exit_code}\n")
    file(WRITE "${stub}" "${text}")
    set(${out_var} "${stub}" PARENT_SCOPE)
endfunction()

# Run the production deployment script against the stand-in.
function(run_deploy stub out_result out_output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DWINDEPLOYQT=${stub}"
            "-DEXECUTABLE=${executable}"
            "-DQT_BIN_DIR=${qt_bin_dir}"
            -P "${DEPLOY_SCRIPT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    # CMake wraps a long message across lines at whitespace, so runs of
    # whitespace are collapsed and the expectations below can be written as the
    # sentences they are. No path here contains a space, so none is damaged.
    string(REGEX REPLACE "[ \t\r\n]+" " " output "${output}")
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

function(expect_failure result output regex what)
    if(result EQUAL 0)
        message(FATAL_ERROR "${what}: deployment was accepted:\n${output}")
    endif()
    if(NOT output MATCHES "${regex}")
        message(FATAL_ERROR
            "${what}: deployment failed but did not explain itself.\n"
            "  expected to match: ${regex}\n"
            "  said:\n${output}")
    endif()
endfunction()

# The exact shape of the message that cost INFRA-QT-DEPLOY-001 a day: a Qt
# binary directory nobody configured, reported as a missing dependency.
set(_misresolved_line
    "Unable to find dependent libraries of C:/nowhere/gnu-16-mingw-amd64/bin/Qt6Core.dll")

if(CASE STREQUAL "deploys-what-it-promised")
    # First the case that must keep working, or the checks below would break
    # every GUI build.
    write_stub("Updating Qt6Core.dll." 0 "Qt6Core.dll" stub)
    run_deploy("${stub}" result output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "a correct deployment was rejected:\n${output}")
    endif()

elseif(CASE STREQUAL "rejects-a-deployment-that-copied-nothing")
    # windeployqt has been seen to exit 0 having copied nothing. The
    # executable links Qt6Core, so that is a broken build tree, not a success.
    write_stub("Nothing to do." 0 "" stub)
    run_deploy("${stub}" result output)
    expect_failure("${result}" "${output}" "deployed no Qt6Core"
        "rejects-a-deployment-that-copied-nothing")

elseif(CASE STREQUAL "names-both-directories-when-the-prefix-is-misresolved")
    write_stub("${_misresolved_line}" 1 "" stub)
    run_deploy("${stub}" result output)
    expect_failure("${result}" "${output}" "8\\.3 short name"
        "names-both-directories-when-the-prefix-is-misresolved")
    # The point of the diagnostic is that a reader can see the two paths.
    foreach(expected "C:/nowhere/gnu-16-mingw-amd64/bin" "${qt_bin_dir}")
        if(NOT output MATCHES "${expected}")
            message(FATAL_ERROR
                "the diagnostic does not name ${expected}:\n${output}")
        endif()
    endforeach()

elseif(CASE STREQUAL "rejects-a-misresolved-prefix-that-reported-success")
    # The dangerous variant: Qt libraries appear, but they came from the wrong
    # prefix. A zero exit code must not be enough to accept that.
    write_stub("${_misresolved_line}" 0 "Qt6Core.dll" stub)
    run_deploy("${stub}" result output)
    expect_failure("${result}" "${output}" "resolved Qt from the wrong"
        "rejects-a-misresolved-prefix-that-reported-success")

elseif(CASE STREQUAL "accepts-the-configured-prefix")
    # And the diagnostic must not fire when windeployqt names the directory Qt
    # really is in: the same message shape, the right path.
    file(TO_CMAKE_PATH "${qt_bin_dir}" reported)
    write_stub("Unable to find dependent libraries of ${reported}/Qt6Core.dll"
        0 "Qt6Core.dll" stub)
    run_deploy("${stub}" result output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "a deployment from the configured prefix was rejected:\n${output}")
    endif()

else()
    message(FATAL_ERROR "DeployFailurePaths.cmake: unknown CASE '${CASE}'")
endif()

message(STATUS "${CASE}: ok")
