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
# P19-ARCH-001 / ADR-059. cfd takes layer 42: above meshing, which it consumes, and
# BELOW thermal (45) and structural (50), because the couplings the roadmap makes likely
# run OUT of CFD (a wall pressure is a structural load, a wall heat-transfer coefficient
# is a thermal boundary condition). Each fixture tree holds exactly one violation.
# The POSITIVE directions -- thermal -> cfd and structural -> cfd -- are proved by the
# `valid` tree, which also keeps `set(layer_cfd 42)` in place.
bettercad_add_process_test(architecture.checker.cfd-layer-violation
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-layer-violation|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'cfd' must not depend on 'io'")
bettercad_add_process_test(architecture.checker.cfd-to-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-to-structural|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'cfd' must not depend on 'structural'")
bettercad_add_process_test(architecture.checker.cfd-to-thermal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-to-thermal|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'cfd' must not depend on 'thermal'")
bettercad_add_process_test(architecture.checker.meshing-to-cfd
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/meshing-to-cfd|-P|${_check_layering}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*'meshing' must not depend on 'cfd'")
bettercad_add_process_test(architecture.checker.cfd-backend-leak
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-backend-leak|-P|${_check_layering}"
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
# P19-ARCH-001 / ADR-059. A link is a second way to depend on a module that no include
# shows: a PRIVATE link from cfd to the thermal or the structural target would give it
# that layer's symbols without one forbidden header.
bettercad_add_process_test(architecture.targets.cfd-links-thermal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-links-thermal|-P|${_check_targets}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*bettercad_cfd links the thermal target")
bettercad_add_process_test(architecture.targets.cfd-links-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-links-structural|-P|${_check_targets}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*bettercad_cfd links the structural target")
bettercad_add_process_test(architecture.targets.cfd-links-io
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/cfd-links-io|-P|${_check_targets}"
    EXIT_CODE 1 STDERR_REGEX "1 violation.*bettercad_cfd names 'BetterCAD::io'")

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

# P18-PERSIST-001: the persisted form of a thermal analysis is canonical intent and nothing else. The check scans
# src/io/json/ThermalAnalysisJson.cpp for its includes (the shared private header and the standard library), for derived
# work and state, mesh identity, material properties, numbers parsed from text and non-determinism, requires it to go
# through the one face encoding and the core's own validators, and scans the rest of the io module for a thermal header
# other than the canonical analysis object. Each fixture tree holds exactly one defect (or none); the valid tree also holds
# comments that name forbidden things, which are not code.
set(_check_thermalpersist "${CMAKE_CURRENT_SOURCE_DIR}/CheckThermalPersistenceOwnership.cmake")
add_test(NAME architecture.thermalpersist-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_thermalpersist}")
bettercad_add_process_test(architecture.thermalpersist-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-valid|-P|${_check_thermalpersist}"
    EXIT_CODE 0 STDOUT_REGEX "Thermal persistence ownership check passed")
bettercad_add_process_test(architecture.thermalpersist-ownership.includes-solver
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-includes-solver|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*ThermalSolve")
bettercad_add_process_test(architecture.thermalpersist-ownership.spaced-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-spaced-include|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*ThermalSolve")
bettercad_add_process_test(architecture.thermalpersist-ownership.lookalike-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-lookalike-include|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*xThermalAnalysisObject")
bettercad_add_process_test(architecture.thermalpersist-ownership.includes-third-party
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-includes-third-party|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*Eigen")
bettercad_add_process_test(architecture.thermalpersist-ownership.other-project-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-other-project-include|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*JsonReader")
bettercad_add_process_test(architecture.thermalpersist-ownership.derived-name
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-derived-name|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*solveSteady")
bettercad_add_process_test(architecture.thermalpersist-ownership.current-check
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-current-check|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*isCurrent")
bettercad_add_process_test(architecture.thermalpersist-ownership.mesh-identity
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-mesh-identity|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*NodeId")
bettercad_add_process_test(architecture.thermalpersist-ownership.material-copy
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-material-copy|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*Conductivity")
bettercad_add_process_test(architecture.thermalpersist-ownership.material-id
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-material-id|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*Material")
bettercad_add_process_test(architecture.thermalpersist-ownership.parses-text
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-parses-text|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*stod")
bettercad_add_process_test(architecture.thermalpersist-ownership.unordered
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-unordered|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*unordered_")
bettercad_add_process_test(architecture.thermalpersist-ownership.timestamp
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-timestamp|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*chrono")
bettercad_add_process_test(architecture.thermalpersist-ownership.stale-flag
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-stale-flag|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*stale")
bettercad_add_process_test(architecture.thermalpersist-ownership.mutable
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-mutable|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*mutable")
bettercad_add_process_test(architecture.thermalpersist-ownership.resolves
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-resolves|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*Document")
bettercad_add_process_test(architecture.thermalpersist-ownership.units
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-units|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*celsius")
bettercad_add_process_test(architecture.thermalpersist-ownership.missing-face-encoding
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-missing-face-encoding|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*faceNameFromJson 0 time")
bettercad_add_process_test(architecture.thermalpersist-ownership.weak-strictness
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-weak-strictness|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*requireObject 1 time")
bettercad_add_process_test(architecture.thermalpersist-ownership.io-includes-solver
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-io-includes-solver|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*ThermalPost")
bettercad_add_process_test(architecture.thermalpersist-ownership.dispatch-derived
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-dispatch-derived|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*solveSteady")
bettercad_add_process_test(architecture.thermalpersist-ownership.vendored-layer-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalpersist-vendored-layer-include|-P|${_check_thermalpersist}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):[ \n]+src/io/json/ThermalAnalysisJson.cpp: includes[ \n]+vendor/bettercad/renderer/Renderer\\.hpp, a header of the derived chain")

# P18-CLI-001: the thermal command line is an adapter over the qualified chain, and the chain's order lives in one place. The check
# scans every apps/bettercad_cli/Thermal*.cpp and .hpp -- literals masked in one pass, comments removed -- for a header of the derived
# chain, a link of it, a material property, a floating-point literal, a decision on a residual or a status written either way round,
# a correction or a math function, a mesh id used as an index or accepted as a target, a file write, a unit conversion, a direct write
# to a process stream, a macro, arithmetic on a quantity (also through a local), and counts the owners each file must use EXACTLY
# where a slack would hide a deleted verdict. It scans src/thermal/ThermalAnalysisRun.cpp for the chain, once each, in order, and for
# any arithmetic, policy, meshing or write; and the header for the exact spelling of `accepted()`. Each fixture tree holds exactly one
# defect (or none); the valid tree also holds comments and strings that name forbidden things, which are not code.
set(_check_thermalcli "${CMAKE_CURRENT_SOURCE_DIR}/CheckThermalCliOwnership.cmake")
add_test(NAME architecture.thermalcli-ownership
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_thermalcli}")
bettercad_add_process_test(architecture.thermalcli-ownership.valid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-valid|-P|${_check_thermalcli}"
    EXIT_CODE 0 STDOUT_REGEX "Thermal CLI ownership check passed")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-solver
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-solver|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/thermal/ThermalSolve\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-system
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-system|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/thermal/ThermalSystem\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-linalg
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-linalg|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/linalg/SparseSolver\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-eigen
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-eigen|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+Eigen/Dense,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-renderer
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-renderer|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/renderer/ThermalContour\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-structural
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-structural|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/structural/StructuralPost\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-other-thermal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-other-thermal|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/thermal/ThermalResult\\.hpp,[ \n]+a[ \n]+thermal[ \n]+header[ \n]+other[ \n]+than")
bettercad_add_process_test(architecture.thermalcli-ownership.includes-lookalike
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-includes-lookalike|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/thermal/xThermalAnalysisRun\\.hpp,[ \n]+a[ \n]+thermal[ \n]+header[ \n]+other[ \n]+than")
bettercad_add_process_test(architecture.thermalcli-ownership.spaced-include
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-spaced-include|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*includes[ \n]+bettercad/thermal/ThermalTransient\\.hpp,[ \n]+a[ \n]+header[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain")
bettercad_add_process_test(architecture.thermalcli-ownership.names-solve
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-names-solve|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+link[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain[ \n]+\\(solveSteady\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.names-assemble
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-names-assemble|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+link[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain[ \n]+\\(assembleThermal\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.names-validate
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-names-validate|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+link[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain[ \n]+\\(validateThermalAnalysis\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.names-gradient
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-names-gradient|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+link[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain[ \n]+\\(gradientFrom\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.names-time-grid
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-names-time-grid|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+link[ \n]+of[ \n]+the[ \n]+derived[ \n]+chain[ \n]+\\(TimeGrid\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.material-property
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-material-property|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+material[ \n]+property[ \n]+\\(Conductivity\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.material-density
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-material-density|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*names[ \n]+a[ \n]+material[ \n]+property[ \n]+\\(density\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.float-tolerance
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-tolerance|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-default
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-default|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-step
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-step|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-no-leading-digit
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-no-leading-digit|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-after-char-literal
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-after-char-literal|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-after-slashes-in-string
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-after-slashes-in-string|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.float-in-a-new-file
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-float-in-a-new-file|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*ThermalExtra\\.cpp:[ \n]+holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-residual
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-residual|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-reversed
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-reversed|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-balance
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-balance|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-balanced-branch
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-balanced-branch|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-status
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-status|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.decision-enumerator
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-decision-enumerator|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*decides[ \n]+on[ \n]+a[ \n]+residual,[ \n]+a[ \n]+balance[ \n]+or[ \n]+a[ \n]+status")
bettercad_add_process_test(architecture.thermalcli-ownership.correction-abs
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-correction-abs|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*corrects[ \n]+or[ \n]+rounds[ \n]+a[ \n]+value[ \n]+\\(abs\\(\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.correction-clamp
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-correction-clamp|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*corrects[ \n]+or[ \n]+rounds[ \n]+a[ \n]+value[ \n]+\\(std::clamp\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.correction-max
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-correction-max|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*corrects[ \n]+or[ \n]+rounds[ \n]+a[ \n]+value[ \n]+\\(std::max\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.math-sqrt
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-math-sqrt|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*calls[ \n]+a[ \n]+math[ \n]+function[ \n]+\\(std::sqrt\\(\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.math-pow
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-math-pow|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*calls[ \n]+a[ \n]+math[ \n]+function[ \n]+\\(std::pow\\(\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.index-node
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-index-node|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*as[ \n]+an[ \n]+index[ \n]+or[ \n]+parks[ \n]+it[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.index-element
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-index-element|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*as[ \n]+an[ \n]+index[ \n]+or[ \n]+parks[ \n]+it[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.index-through-a-local
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-index-through-a-local|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*as[ \n]+an[ \n]+index[ \n]+or[ \n]+parks[ \n]+it[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.option-node
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-option-node|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*accepts[ \n]+a[ \n]+mesh-local[ \n]+target[ \n]+option[ \n]+\\(\"--node\"\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.option-facet
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-option-facet|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*accepts[ \n]+a[ \n]+mesh-local[ \n]+target[ \n]+option[ \n]+\\(\"--facet\"\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.option-dof
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-option-dof|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*accepts[ \n]+a[ \n]+mesh-local[ \n]+target[ \n]+option[ \n]+\\(\"--dof\"\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.persist-ofstream
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-persist-ofstream|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+a[ \n]+file[ \n]+or[ \n]+parses[ \n]+document[ \n]+JSON[ \n]+itself[ \n]+\\(ofstream\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.persist-save
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-persist-save|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+a[ \n]+file[ \n]+or[ \n]+parses[ \n]+document[ \n]+JSON[ \n]+itself[ \n]+\\(saveDocument\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.persist-json
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-persist-json|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+a[ \n]+file[ \n]+or[ \n]+parses[ \n]+document[ \n]+JSON[ \n]+itself[ \n]+\\(nlohmann\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.unit-celsius
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-unit-celsius|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*converts[ \n]+a[ \n]+temperature[ \n]+unit[ \n]+\\(celsius\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.unit-degc
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-unit-degc|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*converts[ \n]+a[ \n]+temperature[ \n]+unit[ \n]+\\(degC\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-cout
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-cout|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+to[ \n]+a[ \n]+process[ \n]+stream[ \n]+directly[ \n]+\\(std::cout\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-printf
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-printf|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+to[ \n]+a[ \n]+process[ \n]+stream[ \n]+directly[ \n]+\\(printf\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-println
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-println|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+to[ \n]+a[ \n]+process[ \n]+stream[ \n]+directly[ \n]+\\(std::print\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-write
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-write|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*writes[ \n]+to[ \n]+a[ \n]+process[ \n]+stream[ \n]+directly[ \n]+\\(\\.write\\(")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-extra-out
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-extra-out|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*3[ \n]+statement\\(s\\)[ \n]+write[ \n]+to[ \n]+out,[ \n]+expected[ \n]+exactly[ \n]+2")
bettercad_add_process_test(architecture.thermalcli-ownership.stream-err
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-stream-err|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*1[ \n]+statement\\(s\\)[ \n]+write[ \n]+to[ \n]+err[ \n]+directly")
bettercad_add_process_test(architecture.thermalcli-ownership.raw-number
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-raw-number|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*JsonValue::number\\([ \n]+appears[ \n]+1[ \n]+time\\(s\\);[ \n]+a[ \n]+double[ \n]+must[ \n]+go[ \n]+through[ \n]+FiniteJson")
bettercad_add_process_test(architecture.thermalcli-ownership.raw-quantity
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-raw-quantity|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*JsonValue::quantity\\([ \n]+appears")
bettercad_add_process_test(architecture.thermalcli-ownership.verdict-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-verdict-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+accepted\\(\\)[ \n]+4[ \n]+time\\(s\\),[ \n]+expected[ \n]+exactly[ \n]+5")
bettercad_add_process_test(architecture.thermalcli-ownership.verdict-extra
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-verdict-extra|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+accepted\\(\\)[ \n]+6[ \n]+time\\(s\\),[ \n]+expected[ \n]+exactly[ \n]+5")
bettercad_add_process_test(architecture.thermalcli-ownership.verdict-return-weakened
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-verdict-return-weakened|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*1[ \n]+exit[ \n]+statement\\(s\\)[ \n]+are[ \n]+exactly")
bettercad_add_process_test(architecture.thermalcli-ownership.run-entry-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-entry-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+runThermalAnalysis[ \n]+0[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+1")
bettercad_add_process_test(architecture.thermalcli-ownership.edits-remove-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-edits-remove-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+RemoveThermalLoadCommand[ \n]+0[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+1")
bettercad_add_process_test(architecture.thermalcli-ownership.edits-execute-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-edits-execute-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+executeEdit\\([ \n]+5[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+6")
bettercad_add_process_test(architecture.thermalcli-ownership.edits-id-cap-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-edits-id-cap-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+kLargestFileId[ \n]+0[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+2")
bettercad_add_process_test(architecture.thermalcli-ownership.nonfinite-check-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-nonfinite-check-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+sawNonFinite\\(\\)[ \n]+5[ \n]+time\\(s\\),[ \n]+expected[ \n]+exactly[ \n]+6")
bettercad_add_process_test(architecture.thermalcli-ownership.refusal-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-refusal-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+refusalOf[ \n]+2[ \n]+time\\(s\\),[ \n]+expected[ \n]+exactly[ \n]+3")
bettercad_add_process_test(architecture.thermalcli-ownership.node-mapping-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-node-mapping-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+nodeOf[ \n]+0[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+1")
bettercad_add_process_test(architecture.thermalcli-ownership.ordinal-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-ordinal-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*uses[ \n]+ordinalOrThrow[ \n]+2[ \n]+time\\(s\\),[ \n]+fewer[ \n]+than[ \n]+the[ \n]+3")
bettercad_add_process_test(architecture.thermalcli-ownership.id-from-position
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-id-from-position|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*4[ \n]+conversion\\(s\\)[ \n]+of[ \n]+a[ \n]+value[ \n]+to[ \n]+std::uint64_t[ \n]+but[ \n]+only[ \n]+3[ \n]+of[ \n]+them[ \n]+are")
bettercad_add_process_test(architecture.thermalcli-ownership.id-from-position-in-place
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-id-from-position-in-place|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*3[ \n]+conversion\\(s\\)[ \n]+of[ \n]+a[ \n]+value[ \n]+to[ \n]+std::uint64_t[ \n]+but[ \n]+only[ \n]+2[ \n]+of[ \n]+them[ \n]+are")
bettercad_add_process_test(architecture.thermalcli-ownership.ordinal-is-id-minus-one
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-ordinal-is-id-minus-one|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*the[ \n]+ordinal[ \n]+lookup[ \n]+lost[ \n]+its[ \n]+answer[ \n]+or[ \n]+its[ \n]+refusal[ \n]+\\(0[ \n]+`return[ \n]+\\*ordinal;`,[ \n]+2[ \n]+defect[ \n]+throw\\(s\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.ordinal-fallback-not-thrown
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-ordinal-fallback-not-thrown|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*the[ \n]+ordinal[ \n]+lookup[ \n]+lost[ \n]+its[ \n]+answer[ \n]+or[ \n]+its[ \n]+refusal[ \n]+\\(1[ \n]+`return[ \n]+\\*ordinal;`,[ \n]+1[ \n]+defect[ \n]+throw\\(s\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.reports-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-reports-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*apps/bettercad_cli/ThermalReports\\.cpp:[ \n]+is[ \n]+missing")
bettercad_add_process_test(architecture.thermalcli-ownership.edits-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-edits-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*apps/bettercad_cli/ThermalEdits\\.cpp:[ \n]+is[ \n]+missing")
bettercad_add_process_test(architecture.thermalcli-ownership.output-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-output-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*apps/bettercad_cli/ThermalOutput\\.hpp:[ \n]+is[ \n]+missing")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-balance-sum
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-balance-sum|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-flux-scale
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-flux-scale|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-unit-rescale
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-unit-rescale|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-through-locals
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-through-locals|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-on-an-arrow
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-on-an-arrow|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.arith-across-a-line
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-arith-across-a-line|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*does[ \n]+arithmetic[ \n]+on[ \n]+a[ \n]+quantity[ \n]+or[ \n]+parks[ \n]+one[ \n]+in[ \n]+a[ \n]+local")
bettercad_add_process_test(architecture.thermalcli-ownership.macro-define
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-macro-define|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*defines[ \n]+a[ \n]+macro[ \n]+or[ \n]+pastes[ \n]+tokens")
bettercad_add_process_test(architecture.thermalcli-ownership.macro-paste
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-macro-paste|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*defines[ \n]+a[ \n]+macro[ \n]+or[ \n]+pastes[ \n]+tokens")
bettercad_add_process_test(architecture.thermalcli-ownership.output-isfinite-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-output-isfinite-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*lost[ \n]+std::isfinite")
bettercad_add_process_test(architecture.thermalcli-ownership.output-tiebreak-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-output-tiebreak-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*lost[ \n]+candidate\\.id[ \n]+<[ \n]+e\\.low\\.id")
bettercad_add_process_test(architecture.thermalcli-ownership.output-tiebreak-high-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-output-tiebreak-high-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*lost[ \n]+candidate\\.id[ \n]+<[ \n]+e\\.high\\.id")
bettercad_add_process_test(architecture.thermalcli-ownership.output-valuefinite-lost
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-output-valuefinite-lost|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*lost[ \n]+valueFinite")
bettercad_add_process_test(architecture.thermalcli-ownership.run-link-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-link-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*assembleThermalSystem[ \n]+is[ \n]+called[ \n]+0[ \n]+time\\(s\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-link-twice
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-link-twice|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*solveSteadyThermal[ \n]+is[ \n]+called[ \n]+2[ \n]+time\\(s\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-order
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-order|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*prepareThermalBoundary[ \n]+is[ \n]+called[ \n]+before[ \n]+prepareThermalLoads")
bettercad_add_process_test(architecture.thermalcli-ownership.run-validate-twice
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-validate-twice|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*validateThermalAnalysis[ \n]+is[ \n]+called[ \n]+2[ \n]+time\\(s\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-validate-blind
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-validate-blind|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*is[ \n]+not[ \n]+handed[ \n]+derivedState\\(\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*src/thermal/ThermalAnalysisRun\\.cpp:[ \n]+is[ \n]+missing")
bettercad_add_process_test(architecture.thermalcli-ownership.run-float
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-float|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+floating-point[ \n]+literal")
bettercad_add_process_test(architecture.thermalcli-ownership.run-math
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-math|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*calls[ \n]+a[ \n]+math[ \n]+function[ \n]+\\(std::sqrt\\(\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-tolerance
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-tolerance|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+tolerance,[ \n]+a[ \n]+status[ \n]+or[ \n]+severity[ \n]+decision")
bettercad_add_process_test(architecture.thermalcli-ownership.run-status
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-status|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+tolerance,[ \n]+a[ \n]+status[ \n]+or[ \n]+severity[ \n]+decision")
bettercad_add_process_test(architecture.thermalcli-ownership.run-assigns-definition
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-assigns-definition|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+tolerance,[ \n]+a[ \n]+status[ \n]+or[ \n]+severity[ \n]+decision")
bettercad_add_process_test(architecture.thermalcli-ownership.run-builds-settings
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-builds-settings|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+tolerance,[ \n]+a[ \n]+status[ \n]+or[ \n]+severity[ \n]+decision")
bettercad_add_process_test(architecture.thermalcli-ownership.run-clears-the-report
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-clears-the-report|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*holds[ \n]+a[ \n]+tolerance,[ \n]+a[ \n]+status[ \n]+or[ \n]+severity[ \n]+decision")
bettercad_add_process_test(architecture.thermalcli-ownership.run-meshes
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-meshes|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*meshes[ \n]+or[ \n]+writes[ \n]+\\(generate")
bettercad_add_process_test(architecture.thermalcli-ownership.run-writes
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-writes|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*meshes[ \n]+or[ \n]+writes[ \n]+\\(addObject\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.run-edits-definition
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-run-edits-definition|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*meshes[ \n]+or[ \n]+writes[ \n]+\\(setDefinition\\)")
bettercad_add_process_test(architecture.thermalcli-ownership.header-solver-success
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-header-solver-success|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*accepted\\(\\)[ \n]+is[ \n]+not[ \n]+exactly")
bettercad_add_process_test(architecture.thermalcli-ownership.header-validator-only
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-header-validator-only|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*accepted\\(\\)[ \n]+is[ \n]+not[ \n]+exactly")
bettercad_add_process_test(architecture.thermalcli-ownership.header-missing
    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-header-missing|-P|${_check_thermalcli}"
    EXIT_CODE 1 STDERR_REGEX "check failed: 1 violation\\(s\\):.*include/bettercad/thermal/ThermalAnalysisRun\\.hpp:[ \n]+is[ \n]+missing")
