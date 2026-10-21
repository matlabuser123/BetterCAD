# P18-POST-001: who owns the recovery of the derived thermal fields.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckRecoveryOwnership.cmake
#
# THE RULE, STATED ONCE. grad(T) = G Te and q = -k grad(T) are computed in ONE place,
# the thermal module (src/thermal, include/bettercad/thermal). A viewer, a report, a
# command or an exporter CONSUMES the recovered arrays; it does not rebuild them from the
# mesh and the nodal temperatures, because a second computation is a second authority
# that drifts from the first.
#
# Two rules, both checked on the source text with // comments removed:
#
#   1  outside the thermal module no file names the three functions that recompute the
#      fields: `computeTet4Geometry` (P18-ELEM-001's G), `gradientFrom` (the shared gradient
#      path) or `recoverThermalElementFields` (P18-POST-001's kernel). Callers of the module's
#      source-bound entry points (`recoverSteadyThermal`, `recoverTransientThermal`) are
#      not flagged: asking the core for a result is not recomputing it.
#
#   2  the recovery itself does not rewrite P18-ELEM's geometry. `ThermalPost.cpp` and
#      `ThermalPost.hpp` must not contain a Jacobian, an adjugate, a cofactor or a shape
#      gradient, and `ThermalPost.cpp` must reach G through `gradientFrom`.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_recompute "computeTet4Geometry|gradientFrom|recoverThermalElementFields")
set(_geometry "[Jj]acobian|adjugate|cofactor|shapeGradient|ShapeGradient")

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
        string(APPEND _violations "${_file}: GUI or other non-thermal code recomputes the recovered fields (${CMAKE_MATCH_1}); consume the core result instead. ")
    endif()
endforeach()

# Rule 2.
foreach(_file IN ITEMS src/thermal/ThermalPost.cpp include/bettercad/thermal/ThermalPost.hpp)
    if(EXISTS "${SOURCE_DIR}/${_file}")
        _code_of("${SOURCE_DIR}/${_file}" _code)
        if(_code MATCHES "(${_geometry})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: rewrites Tet4 geometry (${CMAKE_MATCH_1}); P18-ELEM-001 owns G. ")
        endif()
    endif()
endforeach()
if(EXISTS "${SOURCE_DIR}/src/thermal/ThermalPost.cpp")
    _code_of("${SOURCE_DIR}/src/thermal/ThermalPost.cpp" _code)
    if(NOT _code MATCHES "gradientFrom")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "src/thermal/ThermalPost.cpp: does not reach G through gradientFrom. ")
    endif()
endif()

if(_count GREATER 0)
    message(FATAL_ERROR "Recovery ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Recovery ownership check passed: ${_scanned} files outside the thermal module scanned, 0 violations")
