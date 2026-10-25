# P18-CLI-001: the thermal command line is an ADAPTER over the qualified chain, and the chain's order lives in one place.
#
# Run with:  cmake -DSOURCE_DIR=<repository root> -P CheckThermalCliOwnership.cmake
#
# THE RULE, STATED ONCE. The CLI parses a command line, loads a document, calls the existing core (the P18-CMD commands for an edit,
# `thermal::runThermalAnalysis` for a result), formats what comes back and exits with the core's verdict. It owns no Tet4 formulation,
# no load or boundary integration, no assembly, no linear solve, no time integration, no recovery of a gradient or a flux, no heat
# balance, no acceptance threshold, no currentness rule, no persistence format, and no default for anything a user did not say. A CLI
# that grew any of them would be a second implementation whose answers could drift from the application's, and the one that drifts
# towards "accepted" ships a wrong temperature.
#
# The check reads source text with string and character literals MASKED (one pass, so a quote inside a literal cannot hide the code
# beside it) and // comments removed, so a help string or a comment can neither hide a defect nor be mistaken for one. It is a
# structural defence, not a proof: the behavioural proof is the equivalence tests, and this is what stops the shape of the code from
# changing underneath them. It scans EVERY apps/bettercad_cli/Thermal*.cpp and .hpp by glob, so a new thermal file is gated the moment it
# exists, and it requires the three that must exist.
#
# Rules on those CLI files:
#
#   1  INCLUDE ISOLATION. No header of the derived chain (element, system, load vector, boundary, solve, transient, sparse, convection,
#      material, target), no linalg, renderer, structural, Eigen, OCCT or Qt header: the CLI reaches the chain through ONE entry.
#   2  NO LINK OF THE CHAIN. It names none of the functions or types that make the result -- preparation, assembly, solving, recovery,
#      balance, validation, the model, the numbering, the quality policy: the only way it gets a result is `runThermalAnalysis`.
#   3  NO MATERIAL PROPERTY. P15 owns k, rho and cp; a CLI that named one could supply a default.
#   4  NO FLOATING-POINT LITERAL (`1.5`, `1e-9`, `.5`). A tolerance, a threshold, a default temperature, a time step, a unit factor or a
#      conversion is a number, and a number in a CLI file is exactly where a second policy would hide. (Integers are counts and sizes.)
#   5  NO DECISION ON A RESIDUAL, A BALANCE OR A STATUS, written either way round: no comparison of one with anything, no status
#      enumerator, no branch on `status()` or on `balanced()`. The exit status is the core's `accepted()`.
#   6  NO SIGN OR VALUE CORRECTION. No abs, clamp, min, max, rounding or nextafter: a signed heat load is printed as given and a bad
#      value is refused by the core, never repaired. No square root, power, exponential or logarithm either (`hypot`, the norm of a stored
#      component triple, is the one function allowed).
#   7  NO MESH IDENTITY USED AS AN INDEX, directly or through a local, and none accepted as a target: `x.value()` is never subscripted
#      and never copied into a local; no option is spelled `--node`, `--facet`, `--element`, `--tet` or `--dof`.
#   8  NO PERSISTENCE OF ITS OWN. It writes no file and parses no thermal JSON: loading is `io::loadDocument` and saving is the edit
#      driver's atomic save.
#   9  NO UNIT CONVERSION. No Celsius, Fahrenheit, degC or 273.15: a temperature is kelvin.
#  10  STREAMS. No std::cout, std::cerr, printf, print, println, puts or write: output goes through the streams the driver passes. In the
#      reports exactly two statements write to `out` (the `emit` helper's) and none writes to `err` (diagnostics go through `failure`).
#  11  NUMBERS ARE FINITE-CHECKED. The reports never name `JsonValue::number(` or `JsonValue::quantity(`: every double goes through
#      FiniteJson, which refuses to write `nan` or `inf` as a number. The output header names them exactly where it must.
#  12  THE CORE'S VERDICT, EXACTLY. `accepted()` is used exactly five times in the reports, the two exit statements are exactly the
#      statement `return run.accepted() ? ExitCode::Success : ExitCode::Failure;`, `sawNonFinite()` six times and `refusalOf` three; and
#      every owner is used at every place it is needed (counted), so deleting one call and keeping another cannot pass.
#  13  NO ARITHMETIC ON A QUANTITY, however it is reached. A value read with `.si()` (or `->si()`) is printed, compared for an extremum,
#      or passed on; it is never added, subtracted, scaled or divided, on one line or across two, and it is never parked in a local
#      variable first. A CLI-only heat-balance sum would reproduce the core's number to the last bit and so survive every equivalence
#      test -- which is why the shape of the code is checked as well.
#  14  NO MACRO TRICKS. No #define and no token pasting: a name the scans look for cannot be assembled out of pieces.
#  15  THE OUTPUT HEADER KEEPS ITS TEETH. It must still test `std::isfinite`, remember it, write `null`, say `valueFinite`, and break a tie
#      by the lower id at both ends of an extremum.
#
# Rules on src/thermal/ThermalAnalysisRun.cpp and its header (the entry itself):
#
#  16  THE CHAIN, ONCE EACH, IN ORDER. requireThermalModel, buildThermalDofMap, prepareThermalLoads, prepareThermalBoundary,
#      assembleThermalSystem, then the steady links (solve, recover, balance) and the transient links (solve, recover, balance), each
#      exactly once, and validateThermalAnalysis exactly once, handed `derivedState()`.
#  17  NO ARITHMETIC AND NO POLICY. No floating literal, no tolerance or threshold constant, no math function, no status or severity
#      enumerator, no construction or assignment of a definition, a setting or a tolerance, and no touching the validator's report
#      (`.findings`, `.checks`, `.clear(`): it reads the definition and passes it on.
#  18  NEVER MESHES, NEVER WRITES. No generate, remesh, modifyObject, addObject, removeObject, setDefinition or file write.
#  19  THE VERDICT. `accepted()` is `!stoppedAt.has_value() && validation.accepted()`, spelled exactly so.
#  20  AN ID BECOMES A NUMBER ONLY AS ITSELF. Every `static_cast<std::uint64_t>(` in the reports is applied to `<name>.value()`; an id is never
#      derived from a position (`i + 1`).
#  21  AN ORDINAL IS THE RESULT'S OWN LOOKUP. `ordinalOrThrow` returns `*ordinal` (once) and both defect fallbacks throw (twice).

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_reports apps/bettercad_cli/ThermalReports.cpp)
set(_edits apps/bettercad_cli/ThermalEdits.cpp)
set(_output apps/bettercad_cli/ThermalOutput.hpp)
set(_run src/thermal/ThermalAnalysisRun.cpp)
set(_run_header include/bettercad/thermal/ThermalAnalysisRun.hpp)
set(_required_cli_files ${_reports} ${_edits} ${_output})

file(GLOB _cli_glob RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/apps/bettercad_cli/Thermal*.cpp" "${SOURCE_DIR}/apps/bettercad_cli/Thermal*.hpp")
set(_cli_files ${_required_cli_files})
foreach(_found IN LISTS _cli_glob)
    list(APPEND _cli_files "${_found}")
endforeach()
list(REMOVE_DUPLICATES _cli_files)
list(SORT _cli_files)

set(_forbidden_includes "bettercad/thermal/Thermal(Element|System|LoadVector|Boundary|Solve|Transient|Sparse|Convection|Material|Target|Dof)\\.hpp|bettercad/(linalg|renderer|structural)/|Eigen|occt|OCC|opencascade|Qt[A-Za-z0-9]*/|QtCore|QtWidgets")
set(_allowed_thermal_includes "ThermalAnalysisObject|ThermalAnalysisRun|ThermalBalance|ThermalCommands|ThermalData|ThermalPost|ThermalValidation")
set(_chain "solveSteady|solveTransient|prepareThermal|assembleThermal|recoverSteady|recoverTransient|recoverThermal|balanceSteady|balanceTransient|steadyHeatBalance|transientStepHeatBalance|validateThermalAnalysis|requireThermalModel|buildThermalDofMap|computeTet4Geometry|gradientFrom|ConductanceMatrix|CapacityMatrix|ThermalHeatVector|ThermalSystem|ThermalModel|PreparedThermal|checkSteadyResidual|checkTransientResiduals|checkHeatBalanceStates|checkPostStates|checkSteadyWellPosedness|checkTransientWellPosedness|judgeSolveEvidence|classifyThermalMesh|ThermalMeshVerdict|balanceWithinTolerance|thermalQualityThresholds|physicalStepResidual|validateSteadySolution|validateTransientStep|analyseAnchoring|TimeGrid")
set(_material "[Cc]onductivity|[Dd]ensity|[Ss]pecificHeat|specific_heat|[Mm]aterial[A-Z(]|ThermalProperties|[Dd]iffusivity")
set(_float_literal "[0-9]\\.[0-9]|[0-9][eE][-+]?[0-9]|[0-9]\\.[eE]|([^A-Za-z0-9_)]|^)\\.[0-9]")
set(_decision "[Rr]esidual[A-Za-z_]*(\\(\\))?[ \t\n]*(<=|>=|<|>)[^<>=]|[Bb]alance[A-Za-z_]*(\\(\\))?[ \t\n]*(<=|>=|<|>)[^<>=]|[^-](<=|>=|<|>)[ \t\n]*[A-Za-z_][A-Za-z0-9_.>()-]*([Rr]esidual|[Bb]alanced?)|ValidationStatus::|CheckStatus::|BalanceStatus::|ValidationSeverity::(Warning|Info)|status\\(\\)[ \t\n]*[=!]=|(if|while|\\?)[^;{]*balanced\\(\\)|[!=][ \t]*[A-Za-z_][A-Za-z0-9_.>()-]*balanced\\(\\)")
set(_correction "abs\\(|fabs|copysign|std::clamp|std::max|std::min|std::round|std::floor|std::ceil|std::trunc|nextafter|std::lround")
set(_cli_math "std::(sqrt|cbrt|pow|exp|log|log10|sin|cos|tan|fmod|expm1|log1p)[ \t]*\\(|[^A-Za-z_](sqrt|pow|exp|log)[ \t]*\\(")
set(_index "\\[[^]]*(\\.|->)value\\(\\)[^]]*\\]|(auto|size_t|uint64_t|uint32_t|int|unsigned|long)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=[^;{]*(\\.|->)value\\(\\)")
set(_arithmetic "(\\.|->)si\\(\\)[ \t\n]*[-+*/][^>=]|[-+*/][ \t\n]*[A-Za-z_][^;,]*(\\.|->)si\\(\\)|(double|auto|float)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=[^;{]*(\\.|->)si\\(\\)")
set(_persistence "ofstream|fstream|fopen|saveDocument|writeFile|nlohmann|ordered_json|json::parse|std::filesystem::(remove|rename|copy)")
set(_units "[Cc]elsius|[Ff]ahrenheit|degC|degF|273\\.15")
set(_streams "std::cout|std::cerr|std::clog|printf|std::print|[^a-z_]puts[ \t]*\\(|putchar|fwrite|fputs|fputc|\\.write[ \t]*\\(|[^A-Za-z_]stdout|[^A-Za-z_]stderr")
set(_macros "#[ \t]*define|##")
set(_math "std::(sqrt|pow|exp|log|sin|cos|tan|hypot|fabs|abs|floor|ceil|fmod|max|min|clamp)\\(|[^A-Za-z_](sqrt|pow|exp|log|hypot)[ \t]*\\(")
set(_run_policy "Tolerance|pivotFloor|kBalance|kThermal|ValidationStatus::|CheckStatus::|ValidationSeverity::|\\.severity|\\.status\\(\\)|\\.findings|\\.checks|\\.clear[ \t]*\\(|ThermalSolverSettings[ \t]*[{(]|ThermalSolverSettings[ \t]+[a-z]|UniformInitialTemperature[ \t]*[{(]|TransientSettings[ \t]*[{(]|ThermalAnalysisDefinition[ \t]*[{(]|definition\\.[A-Za-z_.]+[ \t]*=[^=]")
set(_run_write "generate[ \t]*\\(|remesh|modifyObject|addObject|removeObject|setDefinition|saveDocument|ofstream|fopen")

# Reads a file in ONE pass of masking, so a quote inside a literal cannot hide the code beside it:
#   _raw     the file as it is
#   _text    comments removed, literals kept            (used where the CONTENT of a string is the point: an option spelling)
#   _code    literals masked to "", comments removed, #include lines removed    (used by every other rule)
#   _includes the #include directives, taken from the raw text at the start of a line
function(_text_of path out_raw out_text out_code out_includes)
    file(READ "${path}" _raw)
    string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*include[ \t]*[<\"][^>\"\n]+[>\"]" _inc_lines "${_raw}")
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"|'([^'\\\\\n]|\\\\.)+'" "\"\"" _masked "${_raw}")
    string(REGEX REPLACE "//[^\n]*" "" _code "${_masked}")
    string(REGEX REPLACE "#[ \t]*include[^\n]*" "" _code "${_code}")
    string(REGEX REPLACE "//[^\n]*" "" _text "${_raw}")
    set(${out_raw} "${_raw}" PARENT_SCOPE)
    set(${out_text} "${_text}" PARENT_SCOPE)
    set(${out_code} "${_code}" PARENT_SCOPE)
    set(${out_includes} "${_inc_lines}" PARENT_SCOPE)
endfunction()

set(_violations "")
set(_count 0)
set(_scanned 0)

macro(_violation text)
    math(EXPR _count "${_count} + 1")
    string(APPEND _violations "${text} ")
endmacro()

function(_uses code name out)
    string(REGEX MATCHALL "${name}" _found "${code}")
    list(LENGTH _found _n)
    set(${out} ${_n} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------------------------------------------------------------
# The CLI files
# ---------------------------------------------------------------------------------------------------------------------------------
foreach(_file IN LISTS _cli_files)
    if(NOT EXISTS "${SOURCE_DIR}/${_file}")
        _violation("${_file}: is missing; the thermal CLI is gone or was moved.")
        continue()
    endif()
    math(EXPR _scanned "${_scanned} + 1")
    _text_of("${SOURCE_DIR}/${_file}" _raw _text _code _includes)

    # Rule 1.
    foreach(_inc IN LISTS _includes)
        string(REGEX REPLACE "^\n?[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]" "\\1" _path "${_inc}")
        string(STRIP "${_path}" _path)
        if(_path MATCHES "(${_forbidden_includes})")
            _violation("${_file}: includes ${_path}, a header of the derived chain or of another layer; the CLI reaches the chain through thermal::runThermalAnalysis only.")
        elseif(_path MATCHES "^bettercad/thermal/" AND NOT _path MATCHES "^bettercad/thermal/(${_allowed_thermal_includes})\\.hpp$")
            _violation("${_file}: includes ${_path}, a thermal header other than the canonical analysis, the run, its result views, the commands and the validation report.")
        endif()
    endforeach()
    # Rule 2.
    if(_code MATCHES "(${_chain})")
        _violation("${_file}: names a link of the derived chain (${CMAKE_MATCH_1}); a result comes from runThermalAnalysis and from nothing else.")
    endif()
    # Rule 3.
    if(_code MATCHES "(${_material})")
        _violation("${_file}: names a material property (${CMAKE_MATCH_1}); P15 is the sole owner of k, rho and cp.")
    endif()
    # Rule 4.
    if(_code MATCHES "(${_float_literal})")
        _violation("${_file}: holds a floating-point literal (${CMAKE_MATCH_1}); a tolerance, a threshold, a default or a conversion factor does not belong to the CLI.")
    endif()
    # Rule 5.
    if(_code MATCHES "(${_decision})")
        _violation("${_file}: decides on a residual, a balance or a status (${CMAKE_MATCH_1}); acceptance is the core's accepted().")
    endif()
    # Rule 6.
    if(_code MATCHES "(${_correction})")
        _violation("${_file}: corrects or rounds a value (${CMAKE_MATCH_1}); a value is printed as given or refused by the core.")
    endif()
    if(_code MATCHES "(${_cli_math})")
        _violation("${_file}: calls a math function (${CMAKE_MATCH_1}); the CLI computes nothing.")
    endif()
    # Rule 7.
    if(_code MATCHES "(${_index})")
        _violation("${_file}: uses an id's value() as an index or parks it in a local (${CMAKE_MATCH_1}); a NodeId or ElementId is never an index.")
    endif()
    if(_text MATCHES "\"--(node|nodes|facet|facets|element|elements|tet|tets|dof|dofs)\"")
        _violation("${_file}: accepts a mesh-local target option (${CMAKE_MATCH_0}); a target is a CAD face.")
    endif()
    # Rule 8.
    if(_code MATCHES "(${_persistence})")
        _violation("${_file}: writes a file or parses document JSON itself (${CMAKE_MATCH_1}); persistence is io and the edit driver's.")
    endif()
    # Rule 9.
    if(_code MATCHES "(${_units})")
        _violation("${_file}: converts a temperature unit (${CMAKE_MATCH_1}); a temperature is kelvin.")
    endif()
    # Rule 10.
    if(_code MATCHES "(${_streams})")
        _violation("${_file}: writes to a process stream directly (${CMAKE_MATCH_1}); output goes through the streams the driver passes.")
    endif()
    # Rule 13.
    if(_code MATCHES "(${_arithmetic})")
        _violation("${_file}: does arithmetic on a quantity or parks one in a local (${CMAKE_MATCH_1}); a value is printed or compared, never computed with.")
    endif()
    # Rule 14.
    if(_code MATCHES "(${_macros})")
        _violation("${_file}: defines a macro or pastes tokens (${CMAKE_MATCH_1}); a name the scans look for must not be assembled from pieces.")
    endif()
endforeach()

# Rules 10-12 on the reports, which are the only file that renders a payload.
if(EXISTS "${SOURCE_DIR}/${_reports}")
    _text_of("${SOURCE_DIR}/${_reports}" _raw _text _code _includes)
    _uses("${_code}" "[^A-Za-z_]out[ \t]*<<" _out_writes)
    if(NOT _out_writes EQUAL 2)
        _violation("${_reports}: ${_out_writes} statement(s) write to out, expected exactly 2 (the emit helper's); anything else can put text on stdout beside the payload.")
    endif()
    _uses("${_code}" "[^A-Za-z_]err[ \t]*<<" _err_writes)
    if(NOT _err_writes EQUAL 0)
        _violation("${_reports}: ${_err_writes} statement(s) write to err directly; diagnostics go through failure().")
    endif()
    _uses("${_code}" "JsonValue::number[ \t]*\\(" _raw_numbers)
    if(NOT _raw_numbers EQUAL 0)
        _violation("${_reports}: JsonValue::number( appears ${_raw_numbers} time(s); a double must go through FiniteJson.")
    endif()
    _uses("${_code}" "JsonValue::quantity[ \t]*\\(" _raw_quantities)
    if(NOT _raw_quantities EQUAL 0)
        _violation("${_reports}: JsonValue::quantity( appears; a quantity must go through FiniteJson, which refuses nan and inf.")
    endif()
    _uses("${_code}" "return[ \t]+run\\.accepted\\(\\)[ \t]*\\?[ \t]*ExitCode::Success[ \t]*:[ \t]*ExitCode::Failure[ \t]*" _verdicts)
    if(NOT _verdicts EQUAL 2)
        _violation("${_reports}: ${_verdicts} exit statement(s) are exactly `return run.accepted() ? ExitCode::Success : ExitCode::Failure`, expected 2 (validate and balance); the verdict is the core's.")
    endif()
    # EXACT counts: a count above the minimum would let one be deleted and the slack hide it.
    set(_exact "accepted\\(\\)=5" "sawNonFinite\\(\\)=6" "refusalOf=3")
    foreach(_entry IN LISTS _exact)
        string(REGEX REPLACE "=[0-9]+$" "" _name "${_entry}")
        string(REGEX REPLACE "^.*=" "" _expected "${_entry}")
        _uses("${_code}" "${_name}" _used)
        if(NOT _used EQUAL _expected)
            string(REPLACE "\\(\\)" "()" _shown "${_name}")
            _violation("${_reports}: uses ${_shown} ${_used} time(s), expected exactly ${_expected}.")
        endif()
    endforeach()
    # Rule 20. AN ID BECOMES A NUMBER ONLY AS ITSELF: every `static_cast<std::uint64_t>(` in the reports is applied to `<name>.value()`
    # (or `<name>-><value()>`, or `<name>[<index>].value()`) and to nothing else, so no id is derived from an ordinal (`i + 1`).
    _uses("${_code}" "static_cast<std::uint64_t>[ \t]*\\(" _casts)
    _uses("${_code}" "static_cast<std::uint64_t>[ \t]*\\([ \t]*[A-Za-z_][A-Za-z0-9_]*([ \t]*\\[[A-Za-z_][A-Za-z0-9_]*\\])?[ \t]*(\\.|->)[ \t]*value[ \t]*\\([ \t]*\\)[ \t]*\\)" _identity_casts)
    if(NOT _casts EQUAL _identity_casts)
        _violation("${_reports}: ${_casts} conversion(s) of a value to std::uint64_t but only ${_identity_casts} of them are `<id>.value()`; an id is printed as the mesh numbered it and is never derived from a position.")
    endif()
    # Rule 21. AN ORDINAL IS THE RESULT'S OWN LOOKUP: ordinalOrThrow returns what ordinalOf found (`return *ordinal;`, exactly once) and
    # both fallbacks (the missing ordinal and the missing node) throw; an id minus one, or an element 0, is not an answer.
    # (no `;` in the pattern: a match holding one would be split into two list elements and counted twice)
    _uses("${_code}" "return[ \t]+\\*ordinal" _ordinal_returns)
    _uses("${_code}" "throw[ \t]+std::logic_error" _defect_throws)
    if(NOT _ordinal_returns EQUAL 1 OR NOT _defect_throws EQUAL 2)
        _violation("${_reports}: the ordinal lookup lost its answer or its refusal (${_ordinal_returns} `return *ordinal;`, ${_defect_throws} defect throw(s); expected 1 and 2); a position comes from ordinalOf and an id never stands for one.")
    endif()
    set(_minimums
        "runThermalAnalysis=1" "thermalAnalysisIds=1" "checkFaceName=4" "validationJson=3" "nodeOf=1" "ordinalOrThrow=3"
        "loadDocument=1" "Mesher=1")
    foreach(_entry IN LISTS _minimums)
        string(REGEX REPLACE "=[0-9]+$" "" _name "${_entry}")
        string(REGEX REPLACE "^.*=" "" _minimum "${_entry}")
        _uses("${_code}" "${_name}" _used)
        if(_used LESS _minimum)
            _violation("${_reports}: uses ${_name} ${_used} time(s), fewer than the ${_minimum} places that need it.")
        endif()
    endforeach()
endif()

if(EXISTS "${SOURCE_DIR}/${_edits}")
    _text_of("${SOURCE_DIR}/${_edits}" _raw _text _code _includes)
    set(_minimums
        "AddThermalLoadCommand=1" "RemoveThermalLoadCommand=1" "AddThermalBoundaryConditionCommand=1"
        "RemoveThermalBoundaryConditionCommand=1" "CreateThermalAnalysisCommand=1" "parseFaceReference=2" "parseQuantity=1"
        "executeEdit\\(=6" "thermalAnalysisIds=1" "kLargestFileId=2")
    foreach(_entry IN LISTS _minimums)
        string(REGEX REPLACE "=[0-9]+$" "" _name "${_entry}")
        string(REGEX REPLACE "^.*=" "" _minimum "${_entry}")
        _uses("${_code}" "${_name}" _used)
        if(_used LESS _minimum)
            string(REPLACE "\\(" "(" _shown "${_name}")
            _violation("${_edits}: uses ${_shown} ${_used} time(s), fewer than the ${_minimum} places that need it.")
        endif()
    endforeach()
endif()

# Rule 15.
if(EXISTS "${SOURCE_DIR}/${_output}")
    _text_of("${SOURCE_DIR}/${_output}" _raw _text _code _includes)
    foreach(_needed "std::isfinite" "sawNonFinite_ = true" "JsonValue::null\\(\\)" "candidate\\.id < e\\.low\\.id" "candidate\\.id < e\\.high\\.id")
        _uses("${_code}" "${_needed}" _used)
        if(_used EQUAL 0)
            string(REPLACE "\\" "" _shown "${_needed}")
            _violation("${_output}: lost ${_shown}; a non-finite number would be written as a number, or an extremum tie would not break by the lower id.")
        endif()
    endforeach()
    # "valueFinite" is a string literal, which the masked code no longer holds.
    if(NOT _text MATCHES "valueFinite")
        _violation("${_output}: lost valueFinite; a non-finite measurement would no longer say that it is one.")
    endif()
endif()

# ---------------------------------------------------------------------------------------------------------------------------------
# The entry itself
# ---------------------------------------------------------------------------------------------------------------------------------
if(NOT EXISTS "${SOURCE_DIR}/${_run}")
    _violation("${_run}: is missing; the CLI would have no production entry to call.")
else()
    math(EXPR _scanned "${_scanned} + 1")
    _text_of("${SOURCE_DIR}/${_run}" _raw _text _code _includes)

    # Rule 16.
    set(_links
        "requireThermalModel" "buildThermalDofMap" "prepareThermalLoads" "prepareThermalBoundary" "assembleThermalSystem"
        "solveSteadyThermal" "recoverSteadyThermal" "recoverSteadyBalance"
        "solveTransientThermal" "recoverTransientThermal" "recoverTransientBalance")
    set(_previous -1)
    set(_previous_name "")
    foreach(_link IN LISTS _links)
        _uses("${_code}" "[^A-Za-z_]${_link}[ \t]*\\(" _n)
        if(NOT _n EQUAL 1)
            _violation("${_run}: ${_link} is called ${_n} time(s); the chain calls each link exactly once.")
            continue()
        endif()
        string(REGEX MATCH "[^A-Za-z_]${_link}[ \t]*\\(" _spaced "${_code}")
        string(FIND "${_code}" "${_spaced}" _at)
        if(_at LESS _previous)
            _violation("${_run}: ${_link} is called before ${_previous_name}; the order of the chain is frozen.")
        endif()
        set(_previous ${_at})
        set(_previous_name "${_link}")
    endforeach()
    _uses("${_code}" "validateThermalAnalysis[ \t]*\\(" _validations)
    if(NOT _validations EQUAL 1)
        _violation("${_run}: validateThermalAnalysis is called ${_validations} time(s), expected exactly once.")
    endif()
    if(NOT _code MATCHES "validateThermalAnalysis[ \t]*\\([^;]*derivedState[ \t]*\\([ \t]*\\)")
        _violation("${_run}: validateThermalAnalysis is not handed derivedState(); the validator must judge exactly what was built.")
    endif()
    # Rule 17.
    if(_code MATCHES "(${_float_literal})")
        _violation("${_run}: holds a floating-point literal (${CMAKE_MATCH_1}); the entry has no arithmetic and no constant.")
    endif()
    if(_code MATCHES "(${_math})")
        _violation("${_run}: calls a math function (${CMAKE_MATCH_1}); the entry computes nothing.")
    endif()
    if(_code MATCHES "(${_run_policy})")
        _violation("${_run}: holds a tolerance, a status or severity decision, touches the validator's report, or builds or assigns a definition or a setting (${CMAKE_MATCH_1}); it reads the definition and passes it on.")
    endif()
    # Rule 18.
    if(_code MATCHES "(${_run_write})")
        _violation("${_run}: meshes or writes (${CMAKE_MATCH_1}); the entry reads the mesh the caller holds and changes nothing.")
    endif()
endif()

# Rule 19.
if(NOT EXISTS "${SOURCE_DIR}/${_run_header}")
    _violation("${_run_header}: is missing.")
else()
    _text_of("${SOURCE_DIR}/${_run_header}" _raw _text _code _includes)
    string(REGEX REPLACE "[ \t\n]+" " " _squashed "${_code}")
    if(NOT _squashed MATCHES "accepted\\(\\) const noexcept \\{ return !stoppedAt\\.has_value\\(\\) && validation\\.accepted\\(\\); \\}")
        _violation("${_run_header}: accepted() is not exactly `!stoppedAt.has_value() && validation.accepted()`; success is a completed chain and the validator's verdict.")
    endif()
endif()

if(_count GREATER 0)
    message(FATAL_ERROR "Thermal CLI ownership check failed: ${_count} violation(s): ${_violations}")
endif()
message(STATUS "Thermal CLI ownership check passed: ${_scanned} files scanned, 0 violations")
