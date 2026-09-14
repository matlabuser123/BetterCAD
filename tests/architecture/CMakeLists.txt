set(_check_layering "${CMAKE_CURRENT_SOURCE_DIR}/CheckLayering.cmake")
set(_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME architecture.layering
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_layering}")

# Self-tests of the checker: each fixture tree contains exactly one kind of
# violation, which must be reported; the "valid" tree must pass.
bettercad_add_process_test(architecture.checker.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/valid|-P|${_check_layering}"
    EXIT_CODE 0 STDOUT_REGEX "0 violations")
bettercad_add_process_test(architecture.checker.occt-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/occt-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*OCCT header <TopoDS_Shape.hxx>")
bettercad_add_process_test(architecture.checker.qt-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/qt-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*Qt header <QString>")
bettercad_add_process_test(architecture.checker.layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'core' must not depend on 'sketch'")
bettercad_add_process_test(architecture.checker.private-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/private-include|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*quoted include")
