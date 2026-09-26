# Script mode (cmake -P): deploy the Qt runtime next to an executable.
#
# Wraps windeployqt so that its advisory messages (for example about the
# optional qttranslations catalog) do not clutter successful builds; the full
# output is shown only if deployment fails.
#
# It also holds windeployqt to its result. windeployqt resolves the Qt binary
# directory through that directory's 8.3 short name, so when the short name is
# ambiguous it looks for Qt6Core.dll under a path that does not exist, and the
# message it prints names a directory nobody configured. The check below turns
# that into a diagnostic that says which two directories collided
# (INFRA-QT-DEPLOY-001).
#
# Inputs: WINDEPLOYQT, EXECUTABLE; QT_BIN_DIR (optional, improves diagnostics)
foreach(required WINDEPLOYQT EXECUTABLE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "DeployQtRuntime.cmake: ${required} is not set")
    endif()
endforeach()

execute_process(
    COMMAND "${WINDEPLOYQT}"
        --no-translations
        --no-compiler-runtime
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        # Headless platform used by automated GUI tests (-platform offscreen).
        --include-plugins qoffscreen
        --verbose 1
        "${EXECUTABLE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)

# Did windeployqt resolve Qt somewhere other than where Qt is? Reported whether
# it failed or not: a deployment assembled from the wrong prefix is worse than
# one that failed.
#
# Only a *Qt* library counts: windeployqt may have something to say about a
# library that is legitimately not in the Qt directory, and that is not this
# defect. The path is matched up to the first space because windeployqt puts
# the rest of the sentence on the same line, so a Qt prefix containing a space
# goes undiagnosed -- the deployment still fails, just with windeployqt's own
# message -- which is the safe way round: a half-matched path would name the
# wrong directory.
set(misresolved "")
if(DEFINED QT_BIN_DIR AND output MATCHES "dependent libraries of ([^ \n]+[/\\\\]Qt6[^ \n]*\\.dll)")
    set(resolved "${CMAKE_MATCH_1}")
    file(TO_CMAKE_PATH "${resolved}" resolved)
    get_filename_component(resolved_dir "${resolved}" DIRECTORY)
    file(TO_CMAKE_PATH "${QT_BIN_DIR}" expected_dir)
    # Compared case-insensitively: both name the same Windows filesystem, and
    # windeployqt prints whatever case it resolved. `if(PATH_EQUAL)` is not
    # used because this script runs as `cmake -P`, where no policies are set
    # and the operator is not recognised.
    string(TOLOWER "${resolved_dir}" resolved_key)
    string(TOLOWER "${expected_dir}" expected_key)
    if(NOT resolved_key STREQUAL expected_key)
        string(APPEND misresolved
            "\nwindeployqt looked for the Qt libraries in a directory that is "
            "not the one Qt is installed in:\n"
            "  looked in: ${resolved_dir}\n"
            "  Qt is in:  ${expected_dir}\n"
            "It resolves that path through its 8.3 short name. If another "
            "directory alongside the Qt prefix shares the first six characters "
            "of its name, the two share one short name and windeployqt reaches "
            "the wrong one. Rename or remove that directory.")
    endif()
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${result}):\n${output}${misresolved}")
endif()

# windeployqt has been known to exit 0 having copied nothing. The executable
# links Qt6Core, so Qt6Core must be beside it or the build tree is not
# runnable.
get_filename_component(deploy_dir "${EXECUTABLE}" DIRECTORY)
file(GLOB deployed_core "${deploy_dir}/Qt6Core*.dll")
if(NOT deployed_core)
    message(FATAL_ERROR
        "windeployqt reported success but deployed no Qt6Core library to "
        "${deploy_dir}, so the executable cannot start.\n${output}${misresolved}")
endif()
if(NOT misresolved STREQUAL "")
    message(FATAL_ERROR
        "windeployqt reported success but resolved Qt from the wrong "
        "directory.${misresolved}\n${output}")
endif()
