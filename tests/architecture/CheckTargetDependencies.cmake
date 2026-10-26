# Script mode (cmake -P): the LINK-GRAPH half of the ADR-050 / ADR-052 gate.
#
# CheckLayering.cmake proves what the modules INCLUDE. A link is a second way to
# depend on a module that no include shows: a PRIVATE link from thermal to the
# structural target would give thermal the structural layer's symbols and its
# static initialisation without one forbidden header, and ADR-050 forbids exactly
# that ("DO NOT hand-copy", "no temporary violation"). So this reads the
# CMakeLists of the modules ADR-052 constrains and asserts the link lists.
#
# Rules
#   1. bettercad_thermal links no bettercad_structural / BetterCAD::structural,
#      public or private (ADR-050).
#   2. bettercad_thermal reaches the generic solve through BetterCAD::linalg
#      (ADR-052): the reuse mechanism must be present, not merely allowed.
#   3. bettercad_linalg links BetterCAD::core and BetterCAD::eigen and NOTHING
#      else: the generic solve names no physics and no other module (ADR-052).
#   4. bettercad_cfd links neither bettercad_thermal nor bettercad_structural,
#      public or private (ADR-059: cfd 42 sits below both, so they may read a CFD
#      result and it may not read theirs, by include or by link).
#
# Comment lines are ignored, so a note that names a forbidden target is not a
# violation. Exits with an error listing every violation.
if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "CheckTargetDependencies.cmake: SOURCE_DIR must name an existing directory")
endif()

# The link targets named in a CMakeLists, comments stripped.
function(link_targets_of file out_var)
    file(STRINGS "${file}" lines)
    set(code)
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "^[ \t]*#")
            list(APPEND code "${line}")
        endif()
    endforeach()
    string(JOIN "\n" text ${code})
    string(REGEX MATCHALL "BetterCAD::[A-Za-z_0-9]+" found "${text}")
    # The structural TARGET NAME is a link dependency too, spelled without the alias.
    if(text MATCHES "bettercad_structural")
        list(APPEND found "BetterCAD::structural")
    endif()
    if(text MATCHES "bettercad_thermal")
        list(APPEND found "BetterCAD::thermal")
    endif()
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

set(violations)

set(thermal_file "${SOURCE_DIR}/src/thermal/CMakeLists.txt")
set(linalg_file "${SOURCE_DIR}/src/linalg/CMakeLists.txt")
set(cfd_file "${SOURCE_DIR}/src/cfd/CMakeLists.txt")
foreach(required "${thermal_file}" "${linalg_file}" "${cfd_file}")
    if(NOT EXISTS "${required}")
        list(APPEND violations "${required}: missing")
    endif()
endforeach()

if(EXISTS "${thermal_file}")
    link_targets_of("${thermal_file}" thermal_targets)
    if("BetterCAD::structural" IN_LIST thermal_targets)
        list(APPEND violations
            "src/thermal/CMakeLists.txt: bettercad_thermal links the structural target (ADR-050: thermal 45 must not depend on structural 50, by include or by link)")
    endif()
    if(NOT "BetterCAD::linalg" IN_LIST thermal_targets)
        list(APPEND violations
            "src/thermal/CMakeLists.txt: bettercad_thermal does not link BetterCAD::linalg (ADR-052: the generic solve is reached through it)")
    endif()
endif()

if(EXISTS "${cfd_file}")
    link_targets_of("${cfd_file}" cfd_targets)
    if("BetterCAD::thermal" IN_LIST cfd_targets)
        list(APPEND violations
            "src/cfd/CMakeLists.txt: bettercad_cfd links the thermal target (ADR-059: cfd 42 must not depend on thermal 45, by include or by link)")
    endif()
    if("BetterCAD::structural" IN_LIST cfd_targets)
        list(APPEND violations
            "src/cfd/CMakeLists.txt: bettercad_cfd links the structural target (ADR-059: cfd 42 must not depend on structural 50, by include or by link)")
    endif()
    # An ALLOW-LIST as well as the two named refusals: cfd may name core, features, meshing, linalg (once
    # P19-LINSOLVE-001 admits it, PRIVATE), eigen and the compiler options, and nothing else -- not io, not
    # renderer, not scripting. (thermal and structural are reported above, once, with their own message.)
    list(REMOVE_DUPLICATES cfd_targets)
    set(cfd_allowed BetterCAD::core BetterCAD::features BetterCAD::meshing BetterCAD::linalg BetterCAD::eigen BetterCAD::compiler_options BetterCAD::cfd BetterCAD::thermal BetterCAD::structural)
    foreach(target IN LISTS cfd_targets)
        if(NOT target IN_LIST cfd_allowed)
            list(APPEND violations
                "src/cfd/CMakeLists.txt: bettercad_cfd names '${target}' (ADR-059: only core, features, meshing, linalg and eigen)")
        endif()
    endforeach()
endif()

if(EXISTS "${linalg_file}")
    link_targets_of("${linalg_file}" linalg_targets)
    list(REMOVE_DUPLICATES linalg_targets)
    set(allowed BetterCAD::core BetterCAD::eigen BetterCAD::compiler_options)
    foreach(target IN LISTS linalg_targets)
        if(NOT target IN_LIST allowed)
            list(APPEND violations
                "src/linalg/CMakeLists.txt: bettercad_linalg names '${target}' (ADR-052: only core and Eigen)")
        endif()
    endforeach()
endif()

list(LENGTH violations violation_count)
if(violation_count GREATER 0)
    list(JOIN violations "\n  " violation_text)
    message(FATAL_ERROR "Target dependency check failed: ${violation_count} violation(s)\n  ${violation_text}")
endif()
message(STATUS "Target dependency check passed: thermal -> linalg, no structural link, cfd links neither thermal nor structural, linalg -> core + eigen only")
