# P18-BALANCE-001: who owns the heat accounting.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckBalanceOwnership.cmake
#
# THE RULE, STATED ONCE. The residual `K T - Q`, the reactions read from it, the heat each
# convection face exchanges, the energy a step stores and the sums that make the balance are
# computed in ONE place, the thermal module (src/thermal, include/bettercad/thermal). A viewer,
# a report, a command or an exporter CONSUMES the balance result; it does not rebuild the
# accounting from the system and the solution, because a second computation is a second
# authority that drifts from the first (a different sign, a reaction defined as whatever closes
# the books, a nominal step where the actual one belongs).
#
# Three rules, all on the source text with // comments removed:
#
#   1  outside the thermal module no file names the functions that recompute the accounting:
#      `steadyFullResidual`, `transientFullResidual`, `reactionHeats`, `convectionHeatOf`,
#      `steadyHeatBalance`, `transientStepHeatBalance`, `residualFrom` (P18-BC-001's
#      Kconv T - Qconv), or CALLS `storedEnergyChange(`. Reading the result's `storedEnergyChange`
#      member, or calling the source-bound entry points (`recoverSteadyBalance`,
#      `recoverTransientBalance`), is not recomputing it.
#
#   2  the balance does not rewrite what other milestones own. `ThermalBalance.cpp` and
#      `ThermalBalance.hpp` must not integrate an element, a facet or a load (`computeTet4`,
#      `integrateConvection`, `integrateSurfaceHeatFlux`, `integrateVolumetricHeatSource`,
#      `computeTri3Convection`, `facetNodalHeat`, `elementNodalHeat`), a Jacobian, and must
#      not solve or step in time (`solveSteady`, `solveTransient`, `solveThermal`,
#      `effectiveMatrix`, `reduceThermalSystem`).
#
#   3  the accounting reads what the others published: `ThermalBalance.cpp` must reach the
#      convection exchange through `residualFrom` and the load totals through `heatRate`.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_recompute "steadyFullResidual|transientFullResidual|reactionHeats|convectionHeatOf|steadyHeatBalance|transientStepHeatBalance|residualFrom|storedEnergyChange[ \t]*\\(")
set(_foreign "computeTet4|[Jj]acobian|integrateConvection|integrateSurfaceHeatFlux|integrateVolumetricHeatSource|computeTri3Convection|facetNodalHeat|elementNodalHeat|solveSteady|solveTransient|solveThermal|effectiveMatrix|reduceThermalSystem")

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code: drop // to the end of the line (the repository documents with // and ///).
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

# Rule 1.
file(GLOB_RECURSE _files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/apps/*.cpp" "${SOURCE_DIR}/apps/*.hpp" "${SOURCE_DIR}/apps/*.h" "${SOURCE_DIR}/apps/*.qml"
    "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.hpp" "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/examples/*.cpp" "${SOURCE_DIR}/examples/*.hpp")
list(SORT _files)
foreach(_file IN LISTS _files)
    if(_file MATCHES "^src/thermal/" OR _file MATCHES "^include/bettercad/thermal/")
        continue()
    endif()
    math(EXPR _scanned "${_scanned} + 1")
    _code_of("${SOURCE_DIR}/${_file}" _code)
    if(_code MATCHES "(${_recompute})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: GUI or other non-thermal code recomputes the heat accounting (${CMAKE_MATCH_1}); consume the core result instead. ")
    endif()
endforeach()

# Rule 2.
foreach(_file IN ITEMS src/thermal/ThermalBalance.cpp include/bettercad/thermal/ThermalBalance.hpp)
    if(EXISTS "${SOURCE_DIR}/${_file}")
        _code_of("${SOURCE_DIR}/${_file}" _code)
        if(_code MATCHES "(${_foreign})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: rewrites what another milestone owns (${CMAKE_MATCH_1}); P18-LOAD, P18-BC, P18-ASSEMBLY and the solves own it. ")
        endif()
    endif()
endforeach()

# Rule 3.
if(EXISTS "${SOURCE_DIR}/src/thermal/ThermalBalance.cpp")
    _code_of("${SOURCE_DIR}/src/thermal/ThermalBalance.cpp" _code)
    if(NOT _code MATCHES "residualFrom")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "src/thermal/ThermalBalance.cpp: does not take the convection exchange from residualFrom. ")
    endif()
    if(NOT _code MATCHES "heatRate")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "src/thermal/ThermalBalance.cpp: does not take the load totals from the prepared contributions (heatRate). ")
    endif()
endif()

if(_count GREATER 0)
    message(FATAL_ERROR "Balance ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Balance ownership check passed: ${_scanned} files outside the thermal module scanned, 0 violations")
