# P18-PERSIST-001: the persisted form of a thermal analysis is canonical intent and nothing else.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckThermalPersistenceOwnership.cmake
#
# THE RULE, STATED ONCE. A saved thermal analysis holds the canonical definition and no generated solver or result
# array. The writer's only argument is the analysis object, the reader's only inputs are the JSON, a name and a path, and
# both end in the core's own validators; so neither can depend on whether a solve has run, and nothing derived can be
# named. A persistence file that included a solver, kept a matrix, a temperature or a status flag, named a mesh node,
# copied a material property, parsed a number out of text of its own or wrote a time would be a second authority over
# what the derived layers own.
#
# Seven rules on src/io/json/ThermalAnalysisJson.cpp, all on the source text with // comments removed, plus one on the
# rest of the io module:
#
#   1  INCLUDE ISOLATION. The file includes the shared private header `io/json/ObjectJson.hpp` (which carries the canonical
#      analysis object) and the standard library, and nothing else: no thermal solve, system, load-vector, boundary, DOF,
#      transient, post, balance, validation, result or element header; no renderer, OCCT, Qt, Eigen or mesher header.
#   2  NO DERIVED WORK OR DERIVED STATE. It names no function or type of the derived chain: solving, preparation, assembly,
#      recovery, balance, validation, the model, the numbering, the mesher, the regenerator, a result, a report, a
#      currentness check, a view.
#   3  NO MESH IDENTITY. It names no node, element, DOF or free-equation index, facet, mesh stamp or resolved set: a
#      target is a FaceName and nothing else.
#   4  NO MATERIAL AUTHORITY. It names no conductivity, density, specific heat or material type: P15 is the sole owner of
#      k, rho and cp (P18-MAT-001), and a copy in this schema would be a second one.
#   5  NO NUMBER PARSED FROM TEXT HERE. A value is read through `readNumber` and written as a typed double. No stod,
#      from_chars, strtod, sscanf, istringstream or lexical_cast, and no stream.
#   6  NO NON-DETERMINISM AND NO STATE OF ITS OWN. No unordered container, hash, clock, time, random number, thread,
#      atomic, mutable, const_cast, revision, timestamp or status flag. Equal intent gives equal bytes.
#   7  THE CORE'S RULES AND THE CANONICAL ENCODING. It must read `faceNameToJson` and `faceNameFromJson` (the one face
#      encoding), `thermal::validate` (the core's per-record rules, called and not copied), `ThermalAnalysis::create`
#      (the gate every analysis passes) and `requireObject` (strict unknown-key rejection).
#   8  THE REST OF THE MODULE. No other file under src/io includes a thermal header other than the canonical analysis
#      object: the dispatch in DocumentJson.cpp reaches the thermal module through ObjectJson.hpp and nothing more; and
#      the dispatch and the shared header name no derived work or state either.
#   9  NOTHING IS RESOLVED. The reader and writer take no `Document`, find no object, modify none and regenerate nothing:
#      a reference is preserved and never looked up, so it cannot be rebound.
#  10  NO UNIT CONVERSION AND NO FORMATTED NUMBERS. No Celsius, no 273.15, no 1000 scaling, no `.in(`, no to_string,
#      printf or fixed/precision manipulation: a value is `.si()` out and `fromSi` in, and the formatter is the format-wide one.
#
# The seventh rule counts, not merely looks: each owner must be used at every place that needs it (five strict-key reads,
# two face reads and two face writes, five core validator calls), so deleting one call and keeping another cannot pass.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_file src/io/json/ThermalAnalysisJson.cpp)
set(_allowed_project_includes "io/json/ObjectJson\\.hpp")
set(_forbidden_includes "bettercad/thermal/Thermal(System|Solve|LoadVector|Boundary|Dof|Transient|Post|Balance|Validation|Result|Element|Sparse|Convection|Material|Target|Analysis)\\.hpp|bettercad/(renderer|linalg|meshing/Mesher|features/Regenerator)")
set(_derived "solveSteady|solveTransient|prepareThermal|assembleThermal|recoverSteady|recoverTransient|recoverThermal|balanceSteady|balanceTransient|validateThermalAnalysis|requireThermalModel|ThermalModel|ThermalDofMap|buildThermalDofMap|ThermalSystem|SteadyThermalSolution|TransientThermalSolution|ThermalPostResult|ThermalBalanceResult|ThermalValidationReport|PreparedThermal|ThermalResult|ThermalDerivedState|requireCurrent|isCurrent|staleReasons|currentResultSource|Mesher|Regenerator|regenerateAll|ThermalResultView|ThermalContour|ThermalOverlay|SparseMatrix")
set(_mesh_identity "NodeId|ElementId|ThermalDofIndex|ThermalFreeEquationIndex|MeshStamp|FacetId|facet|VolumeMesh|BoundaryFacet")
set(_material "[Cc]onductivity|[Dd]ensity|[Ss]pecificHeat|specific_heat|[Mm]aterial|[Cc]apacity|[Dd]iffusivity|emissivity|[^A-Za-z]rho[^A-Za-z]|ThermalProperties")
set(_resolution "[^A-Za-z]Document[^A-Za-z]|findObject|modifyObject|[Rr]esolve|regenerat|Regenerator")
set(_units "273\\.15|[Cc]elsius|1000\\.0|1e3|1\\.0e3|\\.in[ \t]*\\(|to_string|printf|std::fixed|setprecision")
set(_text_parse "stod|stold|stof|from_chars|strtod|sscanf|atof|istringstream|stringstream|std::stoi|std::stoll|lexical_cast")
set(_nondeterminism "unordered_|std::hash|chrono|clock|std::time|time[ \t]*\\(|rand[ \t]*\\(|random_device|std::thread|std::async|std::atomic|mutable|const_cast|[Rr]evision|[Tt]imestamp|[Ss]tale|bool[ \t]+(is)?(valid|current|solved|validated)[A-Za-z_]*")
set(_must_read faceNameToJson faceNameFromJson "thermal::validate" "ThermalAnalysis::create" requireObject readNumber)

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code: drop // to the end of the line (the repository documents with // and ///).
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

if(EXISTS "${SOURCE_DIR}/${_file}")
    math(EXPR _scanned "${_scanned} + 1")
    _code_of("${SOURCE_DIR}/${_file}" _code)

    # Rule 1.
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" _includes "${_code}")
    foreach(_inc IN LISTS _includes)
        string(REGEX REPLACE "#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]" "\\1" _path "${_inc}")
        if(_path MATCHES "^(${_forbidden_includes})" OR _path MATCHES "(${_forbidden_includes})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}, a header of the derived chain or of another layer; persistence reaches the canonical analysis only. ")
        elseif(_path MATCHES "(Eigen|occt|OCC|Qt|QtCore|opencascade)")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}, a third-party or rendering header persistence has no business with. ")
        elseif(_inc MATCHES "\"" AND NOT _path MATCHES "^(${_allowed_project_includes})$")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}, a project header other than io/json/ObjectJson.hpp. ")
        elseif(_path MATCHES "^bettercad/" AND NOT _path MATCHES "^bettercad/core/")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}; the canonical analysis arrives through ObjectJson.hpp and no other project header is needed. ")
        endif()
    endforeach()
    # Rule 2.
    if(_code MATCHES "(${_derived})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: names derived work or state (${CMAKE_MATCH_1}); persistence stores intent and the derived chain answers through its own currentness. ")
    endif()
    # Rule 3.
    if(_code MATCHES "(${_mesh_identity})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: names a mesh identity (${CMAKE_MATCH_1}); a target is a FaceName and nothing else. ")
    endif()
    # Rule 4.
    if(_code MATCHES "(${_material})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: names a material property (${CMAKE_MATCH_1}); P15 is the sole owner of k, rho and cp. ")
    endif()
    # Rule 5.
    if(_code MATCHES "(${_text_parse})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: parses a number from text (${CMAKE_MATCH_1}); a value is read through readNumber. ")
    endif()
    # Rule 6.
    if(_code MATCHES "(${_nondeterminism})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: a non-deterministic construct or state of its own (${CMAKE_MATCH_1}). ")
    endif()
    # Rule 9 and 10.
    if(_code MATCHES "(${_resolution})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: resolves or looks something up (${CMAKE_MATCH_1}); a reference is preserved, never looked up, so it cannot be rebound. ")
    endif()
    if(_code MATCHES "(${_units})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: converts a unit or formats a number itself (${CMAKE_MATCH_1}); a value is .si() out and fromSi in. ")
    endif()
    # Rule 7: every owner, and at least as many times as there are places that need it.
    set(_minimums "faceNameToJson=2" "faceNameFromJson=1" "thermal::validate=5" "ThermalAnalysis::create=1" "requireObject=5" "readNumber=8")
    foreach(_entry IN LISTS _minimums)
        string(REGEX REPLACE "=.*$" "" _name "${_entry}")
        string(REGEX REPLACE "^.*=" "" _minimum "${_entry}")
        string(REGEX MATCHALL "${_name}" _uses "${_code}")
        list(LENGTH _uses _used)
        if(_used LESS _minimum)
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: uses ${_name} ${_used} time(s), fewer than the ${_minimum} places that need it. ")
        endif()
    endforeach()
endif()

# Rule 8: the rest of the io module reaches the thermal module through ObjectJson.hpp and nothing more.
file(GLOB_RECURSE _io RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/src/io/*.cpp" "${SOURCE_DIR}/src/io/*.hpp")
list(SORT _io)
foreach(_other IN LISTS _io)
    if(_other STREQUAL _file)
        continue()
    endif()
    math(EXPR _scanned "${_scanned} + 1")
    _code_of("${SOURCE_DIR}/${_other}" _other_code)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"]bettercad/thermal/[^>\"]+[>\"]" _thermal_includes "${_other_code}")
    if(_other MATCHES "src/io/json/(DocumentJson\\.cpp|ObjectJson\\.hpp)" AND _other_code MATCHES "(${_derived})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_other}: names derived work or state (${CMAKE_MATCH_1}); the dispatch carries intent only. ")
    endif()
    foreach(_inc IN LISTS _thermal_includes)
        if(NOT _inc MATCHES "bettercad/thermal/ThermalAnalysisObject\\.hpp[>\"]" OR _inc MATCHES "bettercad/thermal/[A-Za-z0-9_/]+/ThermalAnalysisObject")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_other}: ${_inc} -- the io module may include the canonical analysis object and no other thermal header. ")
        endif()
    endforeach()
endforeach()

if(_count GREATER 0)
    message(FATAL_ERROR "Thermal persistence ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Thermal persistence ownership check passed: ${_scanned} files scanned, 0 violations")
