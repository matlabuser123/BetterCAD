# P19-TURB-ARCH-001: where turbulence may live, which today is nowhere.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckTurbulenceOwnership.cmake
#
# THE RULE, STATED ONCE. ADR-058 defers turbulence in full from P19 v1 and ADR-079 decides that the
# architecture is recorded without types: no mode field, no model enumerator, no turbulence variable, no
# wall-treatment record, no turbulence result array, no flow-regime query. Those absences are claims about
# the whole tree, and a negative claim with no test is prose. This check is the test.
#
# IT GUARDS A BOUNDARY THAT HAS NO FILES ON ONE SIDE YET, which is deliberate. There is no cfd command, no
# cfd persistence and no cfd CLI (P19-CMD-001, P19-PERSIST-001, P19-CLI-001 are unauthorized), so the
# persistence, command and CLI decisions of ADR-079 cannot be violated today. They become violable the day
# those milestones add their first file, which is the day this check starts to matter.
#
# Four rules, all on the source text with comments removed:
#
#   1  NO FILE NAMES A TURBULENCE MODEL QUANTITY. The violation is attributed to the area it is in, so each
#      of ADR-079's decisions has a gate that NAMES it rather than one anonymous ban:
#        canonical intent  CfdAnalysisObject / CfdData        -> decisions 1 and 3
#        persistence       src/io/, include/bettercad/io/     -> decision 7
#        a command         any path matching Commands         -> decision 8
#        the CLI           apps/bettercad_cli/                -> decision 9
#        the GUI           apps/bettercad/                    -> decision 9's sibling rule
#        anywhere else                                        -> ADR-058's deferral itself
#
#   2  NO FILE FIXES A LAMINAR-REGIME THRESHOLD. ADR-058 assigns the Reynolds limit to P19-VALID-001, "from
#      a reference", and ADR-079 C6 forbids this milestone fixing a number.
#
#   3  NO `virtual` UNDER cfd. ADR-079 decision 2 fixes the shape a turbulence model must take -- a free
#      function over plain records, composed into momentum the way ADR-073 composes convection and diffusion
#      -- and records that no `virtual` enters cfd. That was PROSE until a mutation analysis pointed out
#      nothing checked it. Measured: 0 of the 46 cfd files contain `virtual` today, and the module's one
#      strategy seam is a std::function port (ADR-075), so the claim is enforceable as written.
#
#   4  THE BARE TURBULENCE SYMBOLS, INSIDE cfd ONLY. `nut` and `mut` are the conventional symbols for a
#      turbulent viscosity and `epsilon`, `omega` and `tke` for the closure variables, so they are the most
#      idiomatic spelling of the thing ADR-058 defers -- and rule 1 cannot match them tree-wide, because
#      they occur in innocent code elsewhere (the counts below). An adversarial review pointed out that the
#      scope where the ambiguity disappears is the one that matters: MEASURED over the 46 files of
#      src/cfd/ and include/bettercad/cfd/, every one of them occurs ZERO times. So inside cfd they are
#      banned, which closes the hole L8 calls this check's largest at no measured cost today.
#
#      THE COST IS REAL AND DELIBERATE. A legitimate `epsilon` tolerance or `omega` angular frequency
#      written inside cfd in future will trip this, and the author will have to rename it or state why the
#      ban should move. That is the trade this rule makes, and it is made only where a turbulence model
#      would have to live. `closure` is NOT in this rule: it occurs in 7 of the 46 cfd files already --
#      CfdContinuity.cpp, CfdDivergence.cpp and CfdGeometry.cpp among them -- as cfd's own word for a
#      continuity closure.
#
# WHAT RULE 1 DELIBERATELY DOES NOT MATCH TREE-WIDE, AND THE MEASUREMENT THAT DECIDED IT. A gate that
# fires on innocent code is worse than no gate, because it teaches people to widen it (ADR-079 L3).
#
# THESE COUNTS WERE WRONG AND ARE CORRECTED HERE, which an adversarial review caught. They were first
# measured over 1830 files -- apps/, src/, include/, examples/ AND tests/ -- and then quoted against "the
# 600 scanned files", a population this check deliberately does not include tests in. Seven of nine rows
# were wrong by factors of two to ten. Re-measured over exactly the files the glob below matches, with both
# comment forms stripped:
#
#        pattern            in the 600 scanned   in all 1830 incl. tests
#        `tke` / `TKE`              10                  44     case-insensitive substring of ordinary words
#        bare `mut`                  9                  30     mutable, mutex, permutation, commutative
#        `closure`                   8                  17     cfd's OWN word: worstClosure, cellClosure
#        bare `nut`                  3                   7     minute, e.g. src/core/units/UnitCatalog.cpp
#        bare `LES`                  2                   9     FILES, STYLES, e.g. src/io/DxfWriter.cpp
#        bare `epsilon`              1                  11     ordinary numerics, src/thermal/ThermalElement.cpp
#        bare `RANS`                 1                   9     TRANSient, src/thermal/ThermalValidation.cpp
#        bare `omega`                0                   6     see below -- NOT rejected on a count
#        `Reynolds` alone            0                   2     see below -- NOT rejected on a count
#
# TWO OF THESE ARE EXCLUDED ON MEANING AND NOT ON A COUNT, and saying so is the point of correcting the
# table: `omega` and `Reynolds` occur zero times in the scanned set, so "rejected on the count" would be
# false for them. `omega` is ordinary mathematics that a laminar solver may legitimately use; `Reynolds` is
# a dimensionless group, not a turbulence model, and P19-VALID-001 legitimately needs one -- only a
# THRESHOLD is banned, by rule 2. Both are matched INSIDE cfd by rule 4, where that ambiguity does not
# arise.
#
# AND THE ONE EXAMPLE THE REVIEW DISPUTED WAS RIGHT AS WRITTEN. `closure` really does occur in
# src/cfd/CfdContinuity.cpp after both comment forms are stripped, as `worstClosure`; the review placed it
# in CfdDivergence.cpp, which also has it. Checked rather than conceded.
#
# THE PATTERN SET WAS WIDENED AFTER A MUTATION ANALYSIS, and the first version was too narrow in a way worth
# recording: it was camelCase compounds ONLY, while every persisted key in this repository is snake_case
# (`time_step`, `end_time`, `reference_pressure`). So a persistence violation would naturally have been
# written in exactly the dialect the persistence gate could not see. Both dialects are matched now. The
# analysis also found that `kCfdTransitionReynoldsNumber` walked past rule 2's original names, and that
# `effectiveViscosity`, `nuEff`, `wallModel`, `logLaw`, `uTau`, `subgrid` and `dissipation` all passed rule
# 1; every one of them is matched now and every one was measured at 0 occurrences first.
#
# IT IS STILL A TEXT SCAN AND STILL EVADABLE (ADR-079 L2). A turbulent viscosity spelled `nut_` or computed
# without being named passes. The scan raises the cost of a violation; it does not make one impossible.
#
# tests/ IS NOT SCANNED, and must not be. The absence gates of tests/compile_fail/CfdTurbulenceMisuse.cpp
# and the pins of tests/cfd/CfdTurbulenceArchitectureTests.cpp name turbulence on purpose -- a test that
# proves a thing is absent has to be able to say its name.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# Compound identifiers in BOTH dialects. Every alternative measured at 0 occurrences in this tree.
# THREE PATTERNS WERE REMOVED AFTER REVIEW, because they are not turbulence words and banning them
# tree-wide is exactly L3's hazard: `[dD]amping` (ROADMAP.md:423 describes the SHIPPED P13-SOLVE-001 as a
# Gauss-Newton loop with damping), `[dD]issipation` (viscous dissipation is a laminar term) and
# `[kK]ineticEnergy` (ordinary mechanics). All three measured 0, and measuring 0 does not make a word mean
# turbulence. The compound forms that DO mean it -- turbulentKineticEnergy, wallDamping -- are still here.
set(_turbulence
    "[tT]urbulen|TURBULEN|turbulen|[eE]ddyViscosity|eddy_|[kK]Epsilon|k_epsilon|[kK]Omega|k_omega|SpalartAllmaras|Smagorinsky|[sS]ubgrid|SST|sgs|[yY]Plus|y_plus|[yY]Star|[wW]allFunction|wall_function|[wW]allDamping|[wW]allModel|[wW]allLaw|[lL]ogLaw|[lL]awOfTheWall|[mM]ixingLength|[mM]ixingScale|[tT]urbulentKineticEnergy|[tT]urbulentViscosity|[eE]ffectiveViscosity|[aA]pparentViscosity|[nm]uEff|nu_t|[fF]rictionVelocity|[sS]hearVelocity|[uU]Tau|[uU]Star|[tT]auWall")

# A threshold is a decision, and it is P19-VALID-001's. `[tT]urbulentOnset` was REMOVED as an alternative
# here: rule 1's `[tT]urbulen` already matches it, and because _count is incremented per rule, one such
# identifier would have been reported as two violations -- which every fixture's "1 violation" assertion
# would then have failed on, for the wrong reason.
set(_threshold
    "[lL]aminarLimit|[lL]aminarReynolds|[rR]eynoldsLimit|[rR]eynoldsCeiling|[cC]riticalReynolds|[rR]eynolds(Threshold|Onset|Transition|Critical|Number)|(Transition|Critical|Laminar)[rR]eynolds")

# Rule 4's symbols, banned INSIDE cfd only. Each measured at 0 of the 46 cfd files.
set(_bare_in_cfd "nut|mut|epsilon|omega|[tT][kK][eE]|RANS|[lL]ES")

function(_code_of path out)
    file(READ "${path}" _text)
    # Comments are not code. ADR-058, ADR-079 and several cfd headers discuss turbulence in prose, and
    # prose is not a violation. BLOCK COMMENTS ARE STRIPPED TOO: the first version of this check stripped
    # only `//`, so an innocent `/* no kEpsilon here */` anywhere in src/ or include/ would have turned the
    # real-tree gate red -- the exact "a gate that fires on innocent code" failure its own header cites.
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" _text "${_text}")
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

# EVERY C++ EXTENSION, not the four in use. Measured: only .cpp and .hpp exist under these roots today, so
# a declaration in a .inl, .ipp or a C++20 module interface (.ixx, .cppm) would have been invisible.
file(GLOB_RECURSE _files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/apps/*.cpp" "${SOURCE_DIR}/apps/*.hpp" "${SOURCE_DIR}/apps/*.h" "${SOURCE_DIR}/apps/*.qml"
    "${SOURCE_DIR}/apps/*.cc" "${SOURCE_DIR}/apps/*.cxx" "${SOURCE_DIR}/apps/*.inl" "${SOURCE_DIR}/apps/*.ipp"
    "${SOURCE_DIR}/apps/*.ixx" "${SOURCE_DIR}/apps/*.cppm" "${SOURCE_DIR}/apps/*.inc"
    "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/src/*.cc" "${SOURCE_DIR}/src/*.cxx" "${SOURCE_DIR}/src/*.inl" "${SOURCE_DIR}/src/*.ipp"
    "${SOURCE_DIR}/src/*.ixx" "${SOURCE_DIR}/src/*.cppm" "${SOURCE_DIR}/src/*.inc"
    "${SOURCE_DIR}/include/*.hpp" "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/include/*.inl" "${SOURCE_DIR}/include/*.ipp" "${SOURCE_DIR}/include/*.ixx"
    "${SOURCE_DIR}/include/*.cppm" "${SOURCE_DIR}/include/*.inc"
    "${SOURCE_DIR}/examples/*.cpp" "${SOURCE_DIR}/examples/*.hpp"
    "${SOURCE_DIR}/examples/*.cc" "${SOURCE_DIR}/examples/*.cxx" "${SOURCE_DIR}/examples/*.inl")
list(SORT _files)

foreach(_file IN LISTS _files)
    _code_of("${SOURCE_DIR}/${_file}" _code)
    math(EXPR _scanned "${_scanned} + 1")

    # Rule 1, attributed to the area so the message names the decision it breaks.
    if(_code MATCHES "(${_turbulence})")
        set(_hit "${CMAKE_MATCH_1}")
        math(EXPR _count "${_count} + 1")
        # THE PATH BRANCHES COME BEFORE THE NAME BRANCH, which a review caught as a misattribution:
        # apps/bettercad_cli/Commands.hpp is the CLI's own verb table, and testing `Commands` first
        # reported it as a HISTORY violation (decision 8) when the verb table has nothing to do with undo.
        # And the variable table of decision 3 lives in CfdFields.hpp, which the intent branch did not
        # name, so a turbulence row there fell to the catch-all.
        if(_file MATCHES "(CfdAnalysisObject|CfdData|CfdFields)\\.(hpp|cpp)$")
            string(APPEND _violations
                "${_file}: canonical intent or the frozen variable table names a turbulence quantity (${_hit}); a turbulence mode is not a field until a model is qualified and the table is closed, ADR-079 decisions 1 and 3. ")
        elseif(_file MATCHES "^src/io/" OR _file MATCHES "^include/bettercad/io/")
            string(APPEND _violations
                "${_file}: persistence names a turbulence quantity (${_hit}); a model field is derived and is never written, ADR-079 decision 7. ")
        elseif(_file MATCHES "^apps/bettercad_cli/")
            string(APPEND _violations
                "${_file}: the CLI names a turbulence quantity (${_hit}); the CLI prints what the core computed, ADR-079 decision 9. ")
        elseif(_file MATCHES "^apps/bettercad/" OR _file MATCHES "^src/renderer/" OR _file MATCHES "^include/bettercad/renderer/")
            string(APPEND _violations
                "${_file}: a view names a turbulence quantity (${_hit}); a view is read-only over qualified derived data and the GUI owns no CAD state, ADR-079 decision 9. ")
        elseif(_file MATCHES "Commands")
            string(APPEND _violations
                "${_file}: a command names a turbulence quantity (${_hit}); the history holds intent only, ADR-079 decision 8. ")
        else()
            string(APPEND _violations
                "${_file}: turbulence is not authorized anywhere (${_hit}); ADR-058 defers it in full from P19 v1 and ADR-079 adds no types for it. ")
        endif()
    endif()

    # Rule 2.
    if(_code MATCHES "(${_threshold})")
        math(EXPR _count "${_count} + 1")
        string(APPEND _violations
            "${_file}: fixes a laminar-regime threshold (${CMAKE_MATCH_1}); P19-VALID-001 owns it, from a reference, ADR-079 decision 10. ")
    endif()

    # Rules 3 and 4, both scoped to cfd.
    if(_file MATCHES "^src/cfd/" OR _file MATCHES "^include/bettercad/cfd/")
        if(_code MATCHES "virtual")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations
                "${_file}: declares virtual inside cfd; ADR-079 decision 2 fixes a model as a free function over plain records, and the module's one strategy seam is a supplied std::function port (ADR-075). ")
        endif()
        if(_code MATCHES "(${_bare_in_cfd})")
            math(EXPR _count "${_count} + 1")
            string(APPEND _violations
                "${_file}: names a bare turbulence symbol inside cfd (${CMAKE_MATCH_1}); nut, mut, epsilon, omega and tke are the conventional symbols of the closure ADR-058 defers, and all five measured zero in the 46 cfd files. ")
        endif()
    endif()
endforeach()

# A CHECK THAT READ NOTHING IS NOT A CHECK THAT PASSED. tests/cli/ZeroMatchGuard.cmake exists in this
# repository because a qualification script once trusted an exit code having run no test at all; a mutation
# analysis pointed out this check had the same shape, reporting "0 violations" if the glob ever matched
# nothing -- a moved source root, a renamed directory, a SOURCE_DIR that is not the repository root. The
# real-tree ctest additionally asserts a three-digit count through STDOUT_REGEX.
if(_scanned EQUAL 0)
    message(FATAL_ERROR
        "Turbulence ownership check failed: it scanned 0 files, so it proved nothing. "
        "SOURCE_DIR=${SOURCE_DIR} matched none of apps/, src/, include/ or examples/.")
endif()

if(_count GREATER 0)
    message(FATAL_ERROR "Turbulence ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Turbulence ownership check passed: ${_scanned} files scanned, 0 violations")
