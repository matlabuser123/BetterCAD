# P18-VIZ-001: a thermal view is a READ-ONLY VIEW of qualified derived P18 data, never a thermal authority.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckThermalVizOwnership.cmake
#
# THE RULE, STATED ONCE. The renderer's thermal files (src/renderer/Thermal*.cpp, include/bettercad/renderer/
# Thermal*.hpp) and the applications READ what the thermal module published: the nodal temperatures, the stored
# element gradient and heat-flux vectors, the stored heat-flux magnitudes, the prepared loads and boundary
# conditions, the report and the currentness verdicts. They compute no thermal quantity of their own. A view that
# recovers a gradient, applies Fourier's law, balances heat, judges a residual, or converts a unit a second time
# is a second thermal authority, and the one that drifts ships a wrong picture of a right answer.
#
# Nine rules, all on the source text with // comments removed:
#
#   1  no thermal physics: no gradient recovery, no Fourier's law, no heat balance, no solve, no assembly, no
#      element or facet integration, no Jacobian, no validation rule seam, no regeneration.
#   2  no unit conversion of its own: no `toModel`/`toModelLength` (the metres-to-millimetres conversion lives ONCE
#      in the OCCT presentation adapter), no literal 1000 / 1e3 / 0.001 / 1e-3, no Celsius or 273.15 (a temperature
#      is Kelvin end to end).
#   3  no raw identifier is an index: `x[id.value()]` and `x[id.value() - 1]` are forbidden. A sparse NodeId or
#      ElementId is found through the numbering (`dofOf`, `nodeOf`) or POST's `ordinalOf`.
#   4  deterministic: no unordered container, hash, clock or random number.
#   5  read-only: no `const_cast`, no `mutable` in a view file.
#   6  currentness has ONE owner: no thermal view file or thermal application code declares a stale flag of its own; staleness is
#      `thermal::isCurrent` / `staleReasons`, recomputed from the sources on every call.
#   7  the single euclidean-norm exception: `hypot` and `sqrt` appear only where a display norm is the whole
#      point -- the gradient display norm (ThermalResultView.cpp) and the arrow length (ThermalOverlay.cpp).
#   8  every view file reads what it must read from the milestone that owns it (identity, numbering, ordinal,
#      facet linkage, currentness), so the binding cannot be silently dropped.
#   9  every glyph builder keeps its classifier cardinality: ThermalOverlay.cpp reaches
#      `classifyBoundaryCardinality`, so a partial classification cannot read as complete.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_physics "gradientFrom|recoverThermalElementFields|recoverSteady|recoverTransient|solveSteadyThermal|solveTransientThermal|solveThermalAlgebra|solveThermalTransientAlgebra|solveSymmetricSparse|assembleThermalSystem|steadyHeatBalance|transientStepHeatBalance|balanceSteadySolution|balanceTransientSolution|steadyFullResidual|transientFullResidual|residualFrom|computeTet4|[Jj]acobian|integrateConvection|integrateSurfaceHeatFlux|integrateVolumetricHeatSource|facetNodalHeat|elementNodalHeat|[Ff]ourier|validateThermalAnalysis|checkSteadyResidual|checkTransientResiduals|checkHeatBalanceStates|checkPostStates|checkTemperatureStates|classifyThermalMesh|thermalQualityThresholds|balanceWithinTolerance|validateSteadySolution|validateTransientStep|analyseAnchoring")
set(_unit "toModel|[Cc]elsius|273\\.15|(^|[^0-9.eE_])(1000(\\.0*)?|1e3|1\\.0e3|0\\.001|1e-3|1\\.0e-3)([^0-9eE_]|$)")
set(_id_index "value\\(\\)[ \t]*(\\]|[-+][ \t]*[A-Za-z0-9_]+[ \t]*\\])")
set(_nondeterministic "unordered_|std::hash|chrono|clock|rand[ \t]*\\(|random_device")
set(_mutating "const_cast|mutable")
set(_stale_flag "bool[ \t]+(is)?[Ss]tale[A-Za-z_]*[ \t]*[;={,)]")
set(_norm "hypot|sqrt")

# file -> names it must read from the milestone that owns them.
set(_must_read_ThermalInspection.cpp "isCurrent" "staleReasons" "requireThermalModel" "postSourceOf" "ordinalOf" "dofOf")
set(_must_read_ThermalResultView.cpp "describes" "elementIds")
set(_must_read_ThermalOverlay.cpp "describes" "elementIds" "ordinalOf" "sourceOfFacet" "classifyBoundaryCardinality")
set(_must_read_ThermalContour.cpp "nodeOfVertex")

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code: drop // to the end of the line (the repository documents with // and ///).
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

file(GLOB _view_files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/src/renderer/Thermal*.cpp" "${SOURCE_DIR}/include/bettercad/renderer/Thermal*.hpp")
file(GLOB_RECURSE _app_files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/apps/*.cpp" "${SOURCE_DIR}/apps/*.hpp" "${SOURCE_DIR}/apps/*.h" "${SOURCE_DIR}/apps/*.qml")
set(_files ${_view_files} ${_app_files})
list(SORT _files)
foreach(_file IN LISTS _files)
    _code_of("${SOURCE_DIR}/${_file}" _code)
    math(EXPR _scanned "${_scanned} + 1")
    get_filename_component(_name "${_file}" NAME)
    set(_is_view FALSE)
    if(_file IN_LIST _view_files)
        set(_is_view TRUE)
    endif()

    # Rule 1: applications and view files alike.
    if(_code MATCHES "(${_physics})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: thermal physics in a view (${CMAKE_MATCH_1}); a view reads what the thermal module published. ")
    endif()
    # Rule 6: a stale flag of its own, anywhere a view or an application could keep one.
    if((_is_view OR _code MATCHES "[Tt]hermal") AND _code MATCHES "(${_stale_flag})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: declares a stale flag of its own (${CMAKE_MATCH_1}); currentness is thermal::isCurrent, recomputed from the sources. ")
    endif()

    if(NOT _is_view)
        continue()
    endif()

    # Rule 2.
    if(_code MATCHES "(${_unit})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: converts or rescales a unit itself (${CMAKE_MATCH_1}); metres-to-millimetres is the OCCT adapter's toModelLength, a temperature is Kelvin. ")
    endif()
    # Rule 3.
    if(_code MATCHES "(${_id_index})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: a raw identifier used as an index (${CMAKE_MATCH_1}); go through the numbering or ordinalOf. ")
    endif()
    # Rule 4.
    if(_code MATCHES "(${_nondeterministic})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: non-deterministic construct (${CMAKE_MATCH_1}); a view is a function of its inputs. ")
    endif()
    # Rule 5.
    if(_code MATCHES "(${_mutating})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: writes through a const view (${CMAKE_MATCH_1}); rendering never mutates results. ")
    endif()
    # Rule 7.
    if(NOT _name STREQUAL "ThermalResultView.cpp" AND NOT _name STREQUAL "ThermalOverlay.cpp" AND _code MATCHES "(${_norm})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: computes a norm (${CMAKE_MATCH_1}) outside the two documented display-norm sites. ")
    endif()
    # Rules 8 and 9.
    if(DEFINED _must_read_${_name})
        foreach(_needed IN LISTS _must_read_${_name})
            if(NOT _code MATCHES "${_needed}")
                math(EXPR _count "${_count} + 1")
                string(APPEND _violations "${_file}: does not read ${_needed} from the milestone that owns it. ")
            endif()
        endforeach()
    endif()
endforeach()

if(_count GREATER 0)
    message(FATAL_ERROR "Thermal visualization ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Thermal visualization ownership check passed: ${_scanned} files scanned, 0 violations")
