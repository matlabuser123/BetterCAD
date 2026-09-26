# Build-infrastructure tests: where BetterCAD is built, and whether what it
# built can actually run from there (INFRA-QT-DEPLOY-001).
#
# Windows only. The failure these guard against is a Windows 8.3 short-name
# ambiguity, and the stand-in for windeployqt is a .cmd file.
if(NOT WIN32)
    return()
endif()

set(_checks "${CMAKE_CURRENT_SOURCE_DIR}/BuildLocationChecks.cmake")
foreach(case
    short-name-basis
    shadowing-sibling-is-found
    no-shadowing-sibling
    short-path-of-a-generated-name
    short-path-of-an-8dot3-name
    external-root-required
    external-root-accepted
    build-inside-source-tree-rejected)
    add_test(NAME infra.buildlocation.${case}
        COMMAND "${CMAKE_COMMAND}"
            "-DCASE=${case}"
            "-DMODULE_DIR=${PROJECT_SOURCE_DIR}/cmake"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/buildlocation/${case}"
            -P "${_checks}")
    set_tests_properties(infra.buildlocation.${case} PROPERTIES TIMEOUT 120)
endforeach()

set(_deploy_paths "${CMAKE_CURRENT_SOURCE_DIR}/DeployFailurePaths.cmake")
foreach(case
    deploys-what-it-promised
    rejects-a-deployment-that-copied-nothing
    names-both-directories-when-the-prefix-is-misresolved
    rejects-a-misresolved-prefix-that-reported-success
    accepts-the-configured-prefix)
    add_test(NAME infra.deploy.${case}
        COMMAND "${CMAKE_COMMAND}"
            "-DCASE=${case}"
            "-DDEPLOY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/deploy/${case}"
            -P "${_deploy_paths}")
    set_tests_properties(infra.deploy.${case} PROPERTIES TIMEOUT 120)
endforeach()

# The real thing: deploy the built application into a directory that is in
# neither the source tree nor the build tree, and start it there.
if(TARGET bettercad AND BETTERCAD_WINDEPLOYQT)
    # Somewhere outside both trees. The probe directory is named after a digest
    # of this build directory so that two build trees on one machine -- the
    # three qualification presets, for instance -- cannot deploy into the same
    # place, while any one build tree always uses the same path.
    string(SHA256 _probe_digest "${CMAKE_BINARY_DIR}")
    string(SUBSTRING "${_probe_digest}" 0 12 _probe_digest)
    file(TO_CMAKE_PATH "$ENV{TEMP}" _probe_base)
    if(_probe_base STREQUAL "")
        set(_probe_base "$ENV{HOME}")
    endif()

    add_test(NAME infra.deploy.location-independent
        COMMAND "${CMAKE_COMMAND}"
            "-DDEPLOY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
            "-DWINDEPLOYQT=${BETTERCAD_WINDEPLOYQT}"
            "-DQT_BIN_DIR=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
            "-DEXECUTABLE=$<TARGET_FILE:bettercad>"
            "-DBUILD_BIN_DIR=$<TARGET_FILE_DIR:bettercad>"
            "-DPROBE_DIR=${_probe_base}/bettercad-deploy-probe-${_probe_digest}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/DeployLocationIndependence.cmake")
    set_tests_properties(infra.deploy.location-independent PROPERTIES TIMEOUT 300)
endif()
