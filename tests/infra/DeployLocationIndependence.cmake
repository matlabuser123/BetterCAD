# Script mode (cmake -P): the deployed Qt application must run from a
# directory that is in neither the source tree nor the build tree.
#
# INFRA-QT-DEPLOY-001. windeployqt does not read the Qt prefix from the
# executable it is deploying -- it resolves the Qt binary directory through
# that directory's 8.3 short name -- so deployment can fail for reasons that
# have nothing to do with where the executable is, and did: an unrelated
# directory next to the dependency prefix took the same short name, and
# windeployqt reported that Qt6Core.dll was missing from a path nobody had
# configured. The build then produced a GUI executable that could not start.
#
# This test deploys through the *production* deployment script, into a third
# location, and then starts the executable there with Qt nowhere on PATH. If
# deployment resolves Qt wrongly, or copies nothing, the executable does not
# start and this fails.
#
# Inputs: DEPLOY_SCRIPT, WINDEPLOYQT, QT_BIN_DIR, EXECUTABLE, BUILD_BIN_DIR,
#         PROBE_DIR, SOURCE_DIR, BUILD_DIR
foreach(required DEPLOY_SCRIPT WINDEPLOYQT QT_BIN_DIR EXECUTABLE BUILD_BIN_DIR
                 PROBE_DIR SOURCE_DIR BUILD_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "DeployLocationIndependence.cmake: ${required} is not set")
    endif()
endforeach()

# TRUE when `path` is `root` or lies below it. An empty relative path means the
# two are the same directory, so equality needs no separate case.
function(is_within path root out_var)
    file(RELATIVE_PATH relative "${root}" "${path}")
    if(relative MATCHES "^\\.\\./" OR IS_ABSOLUTE "${relative}")
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

# The premise of the test: if the probe directory were inside either tree, the
# test would prove nothing.
foreach(tree SOURCE_DIR BUILD_DIR)
    is_within("${PROBE_DIR}" "${${tree}}" within)
    if(within)
        message(FATAL_ERROR
            "The probe directory must be outside the ${tree}, or this test "
            "does not test anything.\n"
            "  probe: ${PROBE_DIR}\n"
            "  ${tree}: ${${tree}}")
    endif()
endforeach()

# A clean probe directory every run: a stale Qt library left by an earlier run
# would let a broken deployment pass.
file(REMOVE_RECURSE "${PROBE_DIR}")
file(MAKE_DIRECTORY "${PROBE_DIR}")

get_filename_component(executable_name "${EXECUTABLE}" NAME)
set(probe_executable "${PROBE_DIR}/${executable_name}")
file(COPY_FILE "${EXECUTABLE}" "${probe_executable}" RESULT copy_error)
if(copy_error)
    message(FATAL_ERROR "cannot copy ${EXECUTABLE}: ${copy_error}")
endif()

# Everything the executable needs *except* Qt: the compiler runtime, Open
# CASCADE, and in a shared build the BetterCAD libraries. Qt is deliberately
# left out -- supplying it is the job under test.
file(GLOB build_libraries "${BUILD_BIN_DIR}/*.dll")
foreach(library IN LISTS build_libraries)
    get_filename_component(library_name "${library}" NAME)
    if(library_name MATCHES "^Qt6")
        continue()
    endif()
    file(COPY_FILE "${library}" "${PROBE_DIR}/${library_name}" RESULT copy_error)
    if(copy_error)
        message(FATAL_ERROR "cannot copy ${library}: ${copy_error}")
    endif()
endforeach()

# The production deployment script, with the production arguments.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DWINDEPLOYQT=${WINDEPLOYQT}"
        "-DEXECUTABLE=${probe_executable}"
        "-DQT_BIN_DIR=${QT_BIN_DIR}"
        -P "${DEPLOY_SCRIPT}"
    RESULT_VARIABLE deploy_result
    OUTPUT_VARIABLE deploy_output
    ERROR_VARIABLE deploy_output
)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR
        "deployment into a directory outside the source and build trees "
        "failed (${deploy_result}):\n${deploy_output}")
endif()

# The platform plugin is what a Qt application cannot start without, and it
# lives in a subdirectory windeployqt has to create, so it is the part most
# likely to be missed by a deployment that resolved the wrong prefix.
foreach(expected "Qt6Core.dll" "platforms/qoffscreen.dll")
    if(NOT EXISTS "${PROBE_DIR}/${expected}")
        message(FATAL_ERROR
            "deployment reported success but did not produce "
            "${expected} in ${PROBE_DIR}")
    endif()
endforeach()

# Start it, with Qt nowhere on PATH: a Qt installation on PATH would hide a
# deployment that copied the wrong libraries or none.
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{SystemRoot}" system_root)
    set(minimal_path "${system_root}/System32")
else()
    set(minimal_path "/usr/bin:/bin")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "--modify" "PATH=set:${minimal_path}"
        "${probe_executable}" --smoke-test -platform offscreen
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_output
    TIMEOUT 120
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "the deployed executable did not start from ${PROBE_DIR} "
        "(${run_result}):\n${run_output}")
endif()

# A whole Qt deployment per build tree is not worth leaving behind once it has
# been shown to work. On any failure above, it is still there to look at.
file(REMOVE_RECURSE "${PROBE_DIR}")

message(STATUS "deployed and ran from ${PROBE_DIR}")
