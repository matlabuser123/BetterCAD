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
# P18-ARCH-001. The thermal module's permitted and forbidden directions, each
# proved by a tree that holds exactly one of them. The POSITIVE direction
# (thermal -> meshing, features, core) is proved by the `valid` tree, which
# also keeps `set(layer_thermal 45)` in place. The SHARP one is
# thermal-to-structural: 45 is below 50 by decision (ADR-050), so the include
# a thermo-mechanical coupling will one day need runs structural -> thermal
# and never the reverse -- and P17's solver API is unreachable from here.
bettercad_add_process_test(architecture.checker.thermal-layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermal-layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'thermal' must not depend on 'io'")
bettercad_add_process_test(architecture.checker.thermal-to-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermal-to-structural|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'thermal' must not depend on 'structural'")
bettercad_add_process_test(architecture.checker.meshing-to-thermal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/meshing-to-thermal|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'meshing' must not depend on 'thermal'")
bettercad_add_process_test(architecture.checker.thermal-backend-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermal-backend-leak|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*mesh backend header <nglib.h>")
# P17-SOLVE-001 / ADR-039. Rule 7: Eigen is PRIVATE to the modules that link
# it, so no PUBLIC header may include it -- otherwise it would propagate to
# io 60, renderer 70, scripting 70, the app and the CLI.
#
# The tree holds BOTH directions deliberately: a public header that includes
# <Eigen/SparseCholesky> and a src/ file that includes <Eigen/SparseCore>. So
# the expected count of ONE proves the rule fires on the public header and does
# not fire on the permitted use -- a rule that flagged both would report two.
bettercad_add_process_test(architecture.checker.eigen-public-header
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/eigen-public-header|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*Eigen header <Eigen/SparseCholesky>")

# P18-SOLVE-001 / ADR-052. The generic symmetric solve was extracted from
# structural (50) into linalg (5) so that thermal (45) can call the SAME
# qualified kernel without including or linking structural. Two halves:
#
#   includes   linalg may include core and nothing above it: a thermal or a
#              structural header from linalg would make the generic kernel
#              physical and recreate the cycle. thermal -> structural is already
#              refused by architecture.checker.thermal-to-structural.
#   links      CheckTargetDependencies.cmake reads the CMakeLists of thermal and
#              linalg: a PRIVATE link to the structural target gives thermal the
#              structural layer without a forbidden include, and is exactly what
#              ADR-050 forbids. Each fixture holds exactly one violation.
bettercad_add_process_test(architecture.checker.linalg-to-thermal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/linalg-to-thermal|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'linalg' must not depend on 'thermal'")
bettercad_add_process_test(architecture.checker.linalg-to-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/linalg-to-structural|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'linalg' must not depend on 'structural'")
set(_check_targets "${CMAKE_CURRENT_SOURCE_DIR}/CheckTargetDependencies.cmake")
add_test(NAME architecture.targets.link-graph
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_targets}")
bettercad_add_process_test(architecture.targets.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/targets-valid|-P|${_check_targets}"
    EXIT_CODE 0 STDOUT_REGEX "Target dependency check passed")
bettercad_add_process_test(architecture.targets.thermal-links-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermal-links-structural|-P|${_check_targets}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*bettercad_thermal links the structural target")

# P18-POST-001. grad(T) and q = -k grad(T) are computed in ONE place, the thermal module;
# a viewer, a report or a command consumes the recovered arrays and never recomputes them
# from the mesh. CheckRecoveryOwnership.cmake scans every file outside the thermal module
# for the three names that recompute the fields, and checks the recovery does not rewrite
# P18-ELEM-001's geometry. Each fixture tree holds exactly one violation (or none): the
# valid tree also holds a comment that names a forbidden function and a viewer that calls
# the core's source-bound entry point, neither of which may be flagged.
set(_check_recovery "${CMAKE_CURRENT_SOURCE_DIR}/CheckRecoveryOwnership.cmake")
add_test(NAME architecture.recovery-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_recovery}")
bettercad_add_process_test(architecture.recovery-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/recovery-valid|-P|${_check_recovery}"
    EXIT_CODE 0 STDOUT_REGEX "Recovery ownership check passed")
bettercad_add_process_test(architecture.recovery-ownership.gui-recomputes
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/recovery-gui-recomputes|-P|${_check_recovery}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*recomputes the recovered fields")
bettercad_add_process_test(architecture.recovery-ownership.rewrites-geometry
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/recovery-rewrites-geometry|-P|${_check_recovery}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*rewrites Tet4 geometry")

# P18-BALANCE-001. The residual K T - Q, the reactions read from it, the heat each convection face
# exchanges, the energy a step stores and the sums that make the balance are computed in ONE place,
# the thermal module; a viewer, a report or a command consumes the balance result and never rebuilds
# the accounting. CheckBalanceOwnership.cmake scans every file outside the thermal module for the
# functions that recompute it, checks the balance does not rewrite what P18-LOAD, P18-BC, P18-ASSEMBLY
# or the solves own, and checks it reads the convection exchange and the load totals from them. Each
# fixture tree holds exactly one defect (or none): the valid tree also holds a comment that names a
# forbidden function, a viewer that calls the core's source-bound entry point and reads a result member
# called storedEnergyChange, none of which may be flagged.
set(_check_balance "${CMAKE_CURRENT_SOURCE_DIR}/CheckBalanceOwnership.cmake")
add_test(NAME architecture.balance-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_balance}")
bettercad_add_process_test(architecture.balance-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/balance-valid|-P|${_check_balance}"
    EXIT_CODE 0 STDOUT_REGEX "Balance ownership check passed")
bettercad_add_process_test(architecture.balance-ownership.gui-recomputes
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/balance-gui-recomputes|-P|${_check_balance}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*recomputes the heat accounting")
bettercad_add_process_test(architecture.balance-ownership.reintegrates
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/balance-reintegrates|-P|${_check_balance}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*rewrites what another milestone owns")
bettercad_add_process_test(architecture.balance-ownership.lacks-attribution
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/balance-lacks-attribution|-P|${_check_balance}"
    EXIT_CODE 1 STDERR_REGEX "2 violation.*does not take the convection exchange")


# P18-VALID-001. Whether a thermal analysis is ACCEPTED is decided in ONE place, validateThermalAnalysis in the
# thermal module; the solver, the recovery, the balance, a viewer or a command consumes its verdict and keeps no
# threshold, severity or pass/fail of its own. CheckValidationOwnership.cmake scans every file outside the thermal
# module for the seams and constants that decide, checks the validator does not rerun or rewrite what the other
# milestones own, adds no tolerance of its own, reads each owner's published function, is deterministic, and that
# no other thermal file defines a competing acceptance API. Each fixture tree holds exactly one defect (or none):
# the valid tree also holds comments that name forbidden things, and a viewer that calls the core's entry point and
# reads a report member, none of which may be flagged.
set(_check_validation "${CMAKE_CURRENT_SOURCE_DIR}/CheckValidationOwnership.cmake")
add_test(NAME architecture.validation-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_validation}")
bettercad_add_process_test(architecture.validation-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-valid|-P|${_check_validation}"
    EXIT_CODE 0 STDOUT_REGEX "Validation ownership check passed")
bettercad_add_process_test(architecture.validation-ownership.gui-decides
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-gui-decides|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*decides thermal acceptance")
bettercad_add_process_test(architecture.validation-ownership.reruns
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-reruns|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*reruns or rewrites")
bettercad_add_process_test(architecture.validation-ownership.second-tolerance
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-second-tolerance|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*carries a tolerance literal")
bettercad_add_process_test(architecture.validation-ownership.lacks-owners
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-lacks-owners|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "2 violation.*does not read")
bettercad_add_process_test(architecture.validation-ownership.second-owner
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-second-owner|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*a second acceptance API")
bettercad_add_process_test(architecture.validation-ownership.unordered
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/validation-unordered|-P|${_check_validation}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*non-deterministic construct")

# P18-VIZ-001: a thermal view is a READ-ONLY VIEW of qualified derived data. The check scans the renderer's thermal
# files and the applications: no thermal physics, no unit conversion of its own, no raw identifier used as an index,
# no non-determinism, no write through a const view, no stale flag of its own, a norm only at the two documented
# display-norm sites, and every view file reads its binding from the milestone that owns it. Each fixture tree holds
# exactly one defect (or none): the valid tree also holds comments that name forbidden things, which are not code.
set(_check_thermalviz "${CMAKE_CURRENT_SOURCE_DIR}/CheckThermalVizOwnership.cmake")
add_test(NAME architecture.thermalviz-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_thermalviz}")
bettercad_add_process_test(architecture.thermalviz-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-valid|-P|${_check_thermalviz}"
    EXIT_CODE 0 STDOUT_REGEX "Thermal visualization ownership check passed")
bettercad_add_process_test(architecture.thermalviz-ownership.gui-recomputes
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-gui-recomputes|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*gradientFrom")
bettercad_add_process_test(architecture.thermalviz-ownership.converts-units
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-converts-units|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*rescales")
bettercad_add_process_test(architecture.thermalviz-ownership.celsius
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-celsius|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*273")
bettercad_add_process_test(architecture.thermalviz-ownership.index-by-id
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-index-by-id|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*raw")
bettercad_add_process_test(architecture.thermalviz-ownership.unordered
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-unordered|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*non-deterministic")
bettercad_add_process_test(architecture.thermalviz-ownership.mutates
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-mutates|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*const_cast")
bettercad_add_process_test(architecture.thermalviz-ownership.stale-flag
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-stale-flag|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*stale")
bettercad_add_process_test(architecture.thermalviz-ownership.second-norm
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-second-norm|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*sqrt")
bettercad_add_process_test(architecture.thermalviz-ownership.lacks-owners
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-lacks-owners|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*ordinalOf")
bettercad_add_process_test(architecture.thermalviz-ownership.no-cardinality
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalviz-no-cardinality|-P|${_check_thermalviz}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*classifyBoundaryCardinality")

# P18-CMD-001: the thermal command layer edits canonical intent and does nothing else. The check scans the two command
# files for their includes (canonical data and the document only), for derived work, mesh identity, positional targeting,
# text round trips and second authorities, requires them to use the one mutation path (modifyObject, setDefinition,
# validate), and scans the rest of the thermal module, the applications and the renderer for a second owner of thermal
# commands or an edit that goes around them. Each fixture tree holds exactly one defect (or none); the valid tree also
# holds comments that name forbidden things, which are not code.
set(_check_thermalcmd "${CMAKE_CURRENT_SOURCE_DIR}/CheckThermalCommandOwnership.cmake")
add_test(NAME architecture.thermalcmd-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_thermalcmd}")
bettercad_add_process_test(architecture.thermalcmd-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-valid|-P|${_check_thermalcmd}"
    EXIT_CODE 0 STDOUT_REGEX "Thermal command ownership check passed")
bettercad_add_process_test(architecture.thermalcmd-ownership.includes-solver
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-includes-solver|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*ThermalSolve")
bettercad_add_process_test(architecture.thermalcmd-ownership.includes-third-party
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-includes-third-party|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*Eigen")
bettercad_add_process_test(architecture.thermalcmd-ownership.solves
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-solves|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*solveSteady")
bettercad_add_process_test(architecture.thermalcmd-ownership.mesh-identity
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-mesh-identity|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*NodeId")
bettercad_add_process_test(architecture.thermalcmd-ownership.positional
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-positional|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*position")
bettercad_add_process_test(architecture.thermalcmd-ownership.parses-text
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-parses-text|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*stod")
bettercad_add_process_test(architecture.thermalcmd-ownership.stale-flag
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-stale-flag|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*authority")
bettercad_add_process_test(architecture.thermalcmd-ownership.unordered
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-unordered|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*unordered")
bettercad_add_process_test(architecture.thermalcmd-ownership.bypasses-path
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-bypasses-path|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "3 violation.*does not use")
bettercad_add_process_test(architecture.thermalcmd-ownership.second-owner
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-second-owner|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*second owner")
bettercad_add_process_test(architecture.thermalcmd-ownership.app-edits
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-app-edits|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*around the commands")
bettercad_add_process_test(architecture.thermalcmd-ownership.quoted-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-quoted-include|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*quoted include")
bettercad_add_process_test(architecture.thermalcmd-ownership.io-edits
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-io-edits|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*around the commands")
bettercad_add_process_test(architecture.thermalcmd-ownership.qualified-edit
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcmd-qualified-edit|-P|${_check_thermalcmd}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*around the commands")
