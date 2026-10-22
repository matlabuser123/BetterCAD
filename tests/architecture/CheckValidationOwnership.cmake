# P18-VALID-001: who owns the decision that a thermal result is ACCEPTED.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckValidationOwnership.cmake
#
# THE RULE, STATED ONCE. Whether a thermal analysis is accepted is decided in ONE place,
# `validateThermalAnalysis` in the thermal module (src/thermal/ThermalValidation.cpp). The solver, the
# recovery, the balance, a viewer, a command and a report CONSUME its verdict; none keeps a threshold, a
# severity or a pass/fail of its own, because a second acceptance policy is a second answer that drifts
# from the first, and the one that drifts towards "accepted" ships a wrong temperature.
#
# Six rules, all on the source text with // comments removed:
#
#   1  outside the thermal module no file names a function or constant that DECIDES acceptance: the rule
#      seams (`checkSteadyResidual`, `checkTransientResiduals`, `checkHeatBalanceStates`, `checkPostStates`,
#      `checkTemperatureStates`, the two well-posedness checks, `checkTransientSetup`, `classifyThermalMesh`),
#      the mesh thresholds (`thermalQualityThresholds`, `kThermalRadiusRatioFloor`, `kThermalAspectRatioCeiling`,
#      `kThermalGradientAccuracy`), or the predecessors' acceptance functions (`balanceWithinTolerance`,
#      `validateSteadySolution`, `validateTransientStep`, `analyseAnchoring`). A consumer calls
#      `validateThermalAnalysis` and reads the report.
#
#   2  the validator JUDGES what exists. `ThermalValidation.cpp` and `.hpp` must not solve, assemble, recover,
#      balance, integrate an element, a facet or a load, compute a Jacobian or a mesh metric, regenerate or
#      remesh, or assign a material.
#
#   3  the validator adds NO tolerance. `ThermalValidation.cpp` must not carry a numeric literal in the
#      1e-9 .. 1e-16 range: the residual tolerance is the canonical setting, the balance tolerance is
#      BALANCE's, and the mesh thresholds are named constants in the header.
#
#   4  the validator reads what the others published: it reaches the steady residual through
#      `validateSteadySolution`, the transient residual through `validateTransientStep`, the balance
#      tolerance through `balanceWithinTolerance`, the datum rule through `analyseAnchoring`, the
#      currentness of every derived object through `requireCurrent`, the model through `requireThermalModel`,
#      the report's own currentness through `staleReasons`, and the mesh metrics through P16's `summaries`
#      and `direction`.
#
#   5  the validator is deterministic: no unordered container, no hash, no clock, no random number.
#
#   6  the thermal module has ONE acceptance API: no other thermal file defines `isAccepted`, `acceptThermal*`,
#      `ThermalAcceptance` or an `overallStatus`.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_decides "checkSteadyResidual|checkTransientResiduals|checkHeatBalanceStates|checkPostStates|checkTemperatureStates|checkSteadyWellPosedness|checkTransientWellPosedness|checkTransientSetup|classifyThermalMesh|thermalQualityThresholds|kThermalRadiusRatioFloor|kThermalAspectRatioCeiling|kThermalGradientAccuracy|balanceWithinTolerance|validateSteadySolution|validateTransientStep|analyseAnchoring")
set(_foreign "computeTet4|[Jj]acobian|integrateConvection|integrateSurfaceHeatFlux|integrateVolumetricHeatSource|computeTri3Convection|facetNodalHeat|elementNodalHeat|gradientFrom|recoverThermalElementFields|recoverSteadyFromSolution|recoverTransientFromSolution|recoverSteadyThermal|recoverTransientThermal|recoverSteadyBalance|recoverTransientBalance|balanceSteadySolution|balanceTransientSolution|solveSteadyThermal|solveSteadyThermalSystem|solveTransientThermal|solveTransientThermalSystem|solveThermalAlgebra|solveThermalTransientAlgebra|solveSymmetricSparse|assembleThermalSystem|steadyHeatBalance|transientStepHeatBalance|steadyFullResidual|transientFullResidual|reactionHeats|convectionHeatOf|storedEnergyChange[ \t]*\\(|residualFrom|evaluateTetQuality|evaluateMeshQuality|generate[ \t]*\\(|regenerateAll|assignMaterial|setMaterial")
set(_tolerance "[0-9]e-(9|1[0-6])")
set(_nondeterministic "unordered_|std::hash|chrono|clock|rand[ \t]*\\(")
set(_second_owner "isAccepted|acceptThermal|ThermalAcceptance|overallStatus")
set(_must_read validateSteadySolution validateTransientStep balanceWithinTolerance analyseAnchoring requireCurrent requireThermalModel staleReasons summaries "direction")

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code: drop // to the end of the line (the repository documents with // and ///).
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

# Rule 1, and rule 6 for the thermal module's other files.
file(GLOB_RECURSE _files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/apps/*.cpp" "${SOURCE_DIR}/apps/*.hpp" "${SOURCE_DIR}/apps/*.h" "${SOURCE_DIR}/apps/*.qml"
    "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.hpp" "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/examples/*.cpp" "${SOURCE_DIR}/examples/*.hpp")
list(SORT _files)
foreach(_file IN LISTS _files)
    _code_of("${SOURCE_DIR}/${_file}" _code)
    if(_file MATCHES "^src/thermal/" OR _file MATCHES "^include/bettercad/thermal/")
        if(NOT _file MATCHES "ThermalValidation\\.(cpp|hpp)$" AND _code MATCHES "(${_second_owner})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: a second acceptance API in the thermal module (${CMAKE_MATCH_1}); P18-VALID-001 is the one owner. ")
        endif()
        continue()
    endif()
    math(EXPR _scanned "${_scanned} + 1")
    if(_code MATCHES "(${_decides})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: non-thermal code decides thermal acceptance (${CMAKE_MATCH_1}); call validateThermalAnalysis and read the report. ")
    endif()
endforeach()

# Rules 2, 3 and 5.
foreach(_file IN ITEMS src/thermal/ThermalValidation.cpp include/bettercad/thermal/ThermalValidation.hpp)
    if(EXISTS "${SOURCE_DIR}/${_file}")
        _code_of("${SOURCE_DIR}/${_file}" _code)
        if(_code MATCHES "(${_foreign})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: reruns or rewrites what another milestone owns (${CMAKE_MATCH_1}); validation judges what exists. ")
        endif()
        if(_code MATCHES "(${_nondeterministic})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: non-deterministic construct (${CMAKE_MATCH_1}); a report is a function of its inputs. ")
        endif()
    endif()
endforeach()
if(EXISTS "${SOURCE_DIR}/src/thermal/ThermalValidation.cpp")
    _code_of("${SOURCE_DIR}/src/thermal/ThermalValidation.cpp" _code)
    if(_code MATCHES "(${_tolerance})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "src/thermal/ThermalValidation.cpp: carries a tolerance literal (${CMAKE_MATCH_1}); the residual tolerance is the canonical setting and the balance tolerance is BALANCE's. ")
    endif()
    # Rule 4.
    foreach(_name IN LISTS _must_read)
        if(NOT _code MATCHES "${_name}")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "src/thermal/ThermalValidation.cpp: does not read ${_name} from the milestone that owns it. ")
        endif()
    endforeach()
endif()

if(_count GREATER 0)
    message(FATAL_ERROR "Validation ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Validation ownership check passed: ${_scanned} files outside the thermal module scanned, 0 violations")
