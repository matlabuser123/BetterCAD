# P18-CMD-001: the thermal command layer edits canonical intent and does nothing else.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckThermalCommandOwnership.cmake
#
# THE RULE, STATED ONCE. The undo history holds canonical thermal intent and never a generated solver or result array.
# A command mutates the analysis definition through the document, once, validated, and every derived P18 object answers
# through its own currentness comparison. A command that solved, prepared, assembled, recovered, balanced, validated,
# remeshed or kept a derived value would be a second authority over what the derived layers own.
#
# Eight rules, all on the source text with // comments removed:
#
#   1  INCLUDE ISOLATION. ThermalCommands.hpp and ThermalCommands.cpp include only core headers, the thermal module's Export
#      and its canonical analysis object (which carries the canonical data), their own header, and the standard library. No
#      solve, system, load-vector, boundary, DOF, transient, post, balance, validation, result or element header; no
#      renderer, OCCT, Qt, Eigen or mesher header.
#   2  NO DERIVED WORK. Neither file names a function or type of the derived chain: solving, preparation, assembly, recovery,
#      balance, validation, the model, the numbering, the mesher, the regenerator, a currentness check, a generation call.
#   3  NO MESH IDENTITY. Neither file names a node, an element, a DOF or free-equation index, a facet, a mesh stamp or a
#      resolved set: a command's targets are stable canonical ids and a FaceName.
#   4  NO POSITIONAL TARGETING. The cpp finds a record by its stable id and never by a position: no operator[], no at(), no
#      iterator arithmetic on the load or condition lists.
#   5  NO TEXT ROUND TRIP. No number is parsed from text (stod, from_chars, strtod, sscanf, istringstream). A value is a
#      typed canonical value, assigned.
#   6  NO SECOND AUTHORITY. No stale flag of its own, no revision written by hand, no const_cast, no mutable, no
#      std::function, no thread, and no unordered container, hash, clock or random number.
#   7  THE ONE PATH. The cpp mutates through `modifyObject` and `setDefinition` and validates through the data model's own
#      `validate`; it must read all three.
#   8  THE ONE OWNER. No other file of the thermal module defines a Command, and no application or renderer file edits a
#      thermal analysis directly: every edit is a command.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_command_files src/thermal/ThermalCommands.cpp include/bettercad/thermal/ThermalCommands.hpp)
set(_allowed_includes "bettercad/core/|bettercad/thermal/Export\\.hpp|bettercad/thermal/ThermalAnalysisObject\\.hpp|bettercad/thermal/ThermalCommands\\.hpp")
set(_derived "solveSteady|solveTransient|prepareThermal|assembleThermal|recoverSteady|recoverTransient|recoverThermal|balanceSteady|balanceTransient|validateThermalAnalysis|requireThermalModel|ThermalModel|ThermalDofMap|buildThermalDofMap|ThermalSystem|SteadyThermalSolution|TransientThermalSolution|ThermalPostResult|ThermalBalanceResult|ThermalValidationReport|PreparedThermal|ThermalResult|requireCurrent|isCurrent|currentResultSource|Mesher|Regenerator|regenerateAll|generate[ \t]*\\(")
set(_mesh_identity "NodeId|ElementId|ThermalDofIndex|ThermalFreeEquationIndex|MeshStamp|FacetId|facet|VolumeMesh|BoundaryFacet")
set(_positional "loads\\[|boundaryConditions\\[|conditions\\[|\\.at[ \t]*\\(|std::next|std::advance|begin\\(\\)[ \t]*\\+")
set(_text_round_trip "stod|stold|stof|from_chars|strtod|sscanf|atof|istringstream|std::stoi|std::stoll")
set(_authority "bool[ \t]+(is)?[Ss]tale[A-Za-z_]*|[Ss]tale[A-Za-z_]*[ \t]*=|setRevision|bumpRevision|incrementRevision|const_cast|mutable|std::function|std::thread|std::async|unordered_|std::hash|chrono|clock|rand[ \t]*\\(|random_device")
set(_must_read modifyObject setDefinition "validate[ \t]*\\(")

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code: drop // to the end of the line (the repository documents with // and ///).
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

# Rules 1 to 7 on the two command files.
foreach(_file IN LISTS _command_files)
    if(NOT EXISTS "${SOURCE_DIR}/${_file}")
        continue()
    endif()
    math(EXPR _scanned "${_scanned} + 1")
    _code_of("${SOURCE_DIR}/${_file}" _code)

    # Rule 1: every #include <bettercad/...> is on the allowed list; no other project header, no third-party header, and no
    # quoted include at all (a relative or private header would bypass the list).
    if(_code MATCHES "#include[ \t]*\"([^\"]+)\"")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: a quoted include (${CMAKE_MATCH_1}) bypasses the allowed list of headers. ")
    endif()
    string(REGEX MATCHALL "#include[ \t]*[<\"][^>\"]+[>\"]" _includes "${_code}")
    foreach(_inc IN LISTS _includes)
        string(REGEX REPLACE "#include[ \t]*[<\"]([^>\"]+)[>\"]" "\\1" _path "${_inc}")
        if(_path MATCHES "^bettercad/" AND NOT _path MATCHES "^(${_allowed_includes})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}, which is not canonical data or the document; a command reaches no derived header. ")
        elseif(_path MATCHES "(Eigen|occt|OCC|Qt|QtCore|opencascade)")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: includes ${_path}, a third-party or rendering header a command has no business with. ")
        endif()
    endforeach()
    # Rule 2.
    if(_code MATCHES "(${_derived})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: names derived work (${CMAKE_MATCH_1}); a command edits intent and the derived chain answers through its own currentness. ")
    endif()
    # Rule 3.
    if(_code MATCHES "(${_mesh_identity})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: names a mesh identity (${CMAKE_MATCH_1}); a command's targets are stable ids and a FaceName. ")
    endif()
    # Rule 5 and 6.
    if(_code MATCHES "(${_text_round_trip})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: parses a number from text (${CMAKE_MATCH_1}); a value is a typed canonical value, assigned. ")
    endif()
    if(_code MATCHES "(${_authority})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "${_file}: a second authority or a non-deterministic construct (${CMAKE_MATCH_1}). ")
    endif()
endforeach()

# Rule 4 and 7 on the cpp only.
if(EXISTS "${SOURCE_DIR}/src/thermal/ThermalCommands.cpp")
    _code_of("${SOURCE_DIR}/src/thermal/ThermalCommands.cpp" _code)
    if(_code MATCHES "(${_positional})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations "src/thermal/ThermalCommands.cpp: addresses a record by position (${CMAKE_MATCH_1}); a command finds it by its stable id. ")
    endif()
    foreach(_name IN LISTS _must_read)
        if(NOT _code MATCHES "${_name}")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "src/thermal/ThermalCommands.cpp: does not use ${_name}, the one path an edit takes. ")
        endif()
    endforeach()
endif()

# Rule 8: no other owner of a thermal command, and nobody edits a thermal analysis around the commands.
file(GLOB_RECURSE _all RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/apps/*.cpp" "${SOURCE_DIR}/apps/*.hpp" "${SOURCE_DIR}/apps/*.h" "${SOURCE_DIR}/apps/*.qml"
    "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.hpp" "${SOURCE_DIR}/include/*.h")
list(SORT _all)
foreach(_file IN LISTS _all)
    if(_file IN_LIST _command_files)
        continue()
    endif()
    _code_of("${SOURCE_DIR}/${_file}" _code)
    if(_file MATCHES "^(src/thermal/|include/bettercad/thermal/)")
        math(EXPR _scanned "${_scanned} + 1")
        if(_code MATCHES "(public[ \t]+Command[ \t]*[{,]|ThermalAnalysisEditCommand)")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: a second owner of thermal commands (${CMAKE_MATCH_1}); ThermalCommands.hpp is the one. ")
        endif()
    elseif(_file MATCHES "^(apps/|src/|include/bettercad/renderer/)")
        # Every application, every source directory and the renderer's headers: a persistence or scripting path that edited a
        # thermal analysis directly would bypass the history just as an application would.
        math(EXPR _scanned "${_scanned} + 1")
        if(_code MATCHES "ThermalAnalysis" AND _code MATCHES "(modifyObject[ \t]*<[^>]*ThermalAnalysis[ \t]*>|->[ \t]*setDefinition[ \t]*\\(|\\.[ \t]*setDefinition[ \t]*\\()")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations "${_file}: edits a thermal analysis around the commands (${CMAKE_MATCH_1}); every edit is a command, so every edit is undoable. ")
        endif()
    endif()
endforeach()

if(_count GREATER 0)
    message(FATAL_ERROR "Thermal command ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Thermal command ownership check passed: ${_scanned} files scanned, 0 violations")
