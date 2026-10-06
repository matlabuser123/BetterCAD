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
bettercad_add_process_test(architecture.checker.examples-occt-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/examples-occt-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*OCCT header <BRepPrimAPI_MakeBox.hxx>")
bettercad_add_process_test(architecture.checker.qt-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/qt-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*Qt header <QString>")
bettercad_add_process_test(architecture.checker.layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'core' must not depend on 'sketch'")
bettercad_add_process_test(architecture.checker.drawing-layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/drawing-layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'drawing' must not depend on 'io'")
bettercad_add_process_test(architecture.checker.mesh-backend-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/mesh-backend-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*mesh backend header <nglib.h>")
bettercad_add_process_test(architecture.checker.mesh-backend-generated-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/mesh-backend-generated-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*mesh backend header <netgen_version.hpp>")
bettercad_add_process_test(architecture.checker.private-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/private-include|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*quoted include")
# P17-ARCH-001. The structural module's permitted and forbidden directions,
# each proved by a tree that holds exactly one of them. The POSITIVE direction
# (structural -> meshing, features, core) is proved by the `valid` tree above,
# which also keeps `set(layer_structural 50)` in place: remove the entry and
# that tree reports an unknown module instead of passing.
#
# There is deliberately no structural-specific Qt fixture: rule 2 keys on a
# header's shape and not on a module, so `architecture.checker.qt-leak` already
# covers every module including this one.
bettercad_add_process_test(architecture.checker.structural-layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/structural-layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'structural' must not depend on 'io'")
bettercad_add_process_test(architecture.checker.meshing-to-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/meshing-to-structural|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'meshing' must not depend on 'structural'")
bettercad_add_process_test(architecture.checker.structural-backend-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/structural-backend-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*mesh backend header <nglib.h>")
bettercad_add_process_test(architecture.checker.library-reaches-apps
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/library-reaches-apps|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*reaches into apps/ from the library")
