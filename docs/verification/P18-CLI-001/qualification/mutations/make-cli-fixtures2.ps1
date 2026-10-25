# Generates tests/architecture/fixtures/thermalcli-*, verifies each against the gate, and rewrites the thermalcli block of
# tests/architecture/CMakeLists.txt. Each case: a name, one change to the valid tree, and the phrase the single violation must contain.
$repo = "C:\Users\hasib\OneDrive\Desktop\CAD"
$fixtures = "$repo\tests\architecture\fixtures"
$gate = "$repo\tests\architecture\CheckThermalCliOwnership.cmake"
$cmakeLists = "$repo\tests\architecture\CMakeLists.txt"
$pk = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$env:PATH = "$pk\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$env:PATH"

$files = [ordered]@{
  'apps/bettercad_cli/ThermalReports.cpp' = @'
// Comments may name thermalConductivity, solveSteadyThermal, 1e-9, std::abs, temperatures[node.value()], degC and std::cout: not code.
#include "Commands.hpp"

#include <bettercad/thermal/ThermalAnalysisRun.hpp>
#include <bettercad/thermal/ThermalPost.hpp>

#include <string>

namespace x {
const char* help = "conductivity 1e-9 degC std::cout std::abs(x) --node 12 solveSteadyThermal residual < 1e-9 it's // not a comment";
const char quote = '"';
void emit(std::ostream& out, bool json) {
    if (json) {
        out << a;
    } else {
        out << b;
    }
}
void f() {
    runThermalAnalysis(d, r, m, a);
    thermalAnalysisIds(d);
    loadDocument(p);
    meshing::Mesher mesher;
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
    run.accepted();
    run.accepted();
    run.accepted();
    checkFaceName(a1);
    checkFaceName(a2);
    checkFaceName(a3);
    checkFaceName(a4);
    validationJson(v1);
    validationJson(v2);
    validationJson(v3);
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    map.nodeOf(i);
    ordinalOrThrow(post, e1);
    ordinalOrThrow(post, e2);
    ordinalOrThrow(post, e3);
    refusalOf(r1);
    refusalOf(r2);
    refusalOf(r3);
    g(static_cast<std::uint64_t>(node->value()));
    g(static_cast<std::uint64_t>(ids[i].value()));
    g(static_cast<std::uint64_t>(ids[j].value()));
    if (!ordinal.has_value()) {
        throw std::logic_error("the element is not in the result");
    }
    if (!node.has_value()) {
        throw std::logic_error("the node is not in the numbering");
    }
    return *ordinal;
}
} // namespace x
'@
  'apps/bettercad_cli/ThermalEdits.cpp' = @'
// Comments may name thermalConductivity, assembleThermalSystem, 293.15, std::clamp, ofstream and saveDocument: not code.
#include "EditSupport.hpp"
#include "Selectors.hpp"

#include <bettercad/thermal/ThermalCommands.hpp>

namespace x {
const char* usage = "--flux <W/m^2> 1e5W/m^3 degC std::abs(x) 273.15";
void f() {
    thermal::AddThermalLoadCommand a;
    thermal::RemoveThermalLoadCommand b;
    thermal::AddThermalBoundaryConditionCommand c;
    thermal::RemoveThermalBoundaryConditionCommand d;
    thermal::CreateThermalAnalysisCommand e;
    parseFaceReference(f1);
    parseFaceReference(f2);
    parseQuantity(q);
    thermalAnalysisIds(doc);
    executeEdit(e1);
    executeEdit(e2);
    executeEdit(e3);
    executeEdit(e4);
    executeEdit(e5);
    executeEdit(e6);
    constexpr auto kLargestFileId = 1;
    if (v > kLargestFileId) {}
}
} // namespace x
'@
  'apps/bettercad_cli/ThermalOutput.hpp' = @'
#pragma once
// A comment naming nan and 1e-9.
#include <cmath>
namespace x {
inline void f() {
    if (!std::isfinite(v)) {
        sawNonFinite_ = true;
        return JsonValue::null();
    }
    entry.set("valueFinite", JsonValue::boolean(ok));
    if (candidate.id < e.low.id) {}
    if (candidate.id < e.high.id) {}
}
} // namespace x
'@
  'src/thermal/ThermalAnalysisRun.cpp' = @'
// Comments may name 1e-9, std::sqrt, Tolerance, generate( and setDefinition: not code.
#include <bettercad/thermal/ThermalAnalysisRun.hpp>

namespace x {
const char* note = "0.5 Tolerance std::sqrt generate( setDefinition";
void f() {
    const auto finish = [&]() {
        run.validation = validateThermalAnalysis(document, regenerator, mesher, analysis, run.derivedState());
    };
    requireThermalModel(a);
    buildThermalDofMap(a);
    prepareThermalLoads(a);
    prepareThermalBoundary(a);
    assembleThermalSystem(a);
    solveSteadyThermal(a);
    recoverSteadyThermal(a);
    recoverSteadyBalance(a);
    solveTransientThermal(a);
    recoverTransientThermal(a);
    recoverTransientBalance(a);
    const auto d = definition.solver;
}
} // namespace x
'@
  'include/bettercad/thermal/ThermalAnalysisRun.hpp' = @'
#pragma once
// A comment naming ValidationStatus::Pass.
struct ThermalAnalysisRun {
    [[nodiscard]] bool accepted() const noexcept { return !stoppedAt.has_value() && validation.accepted(); }
};
'@
}

function Write-Fixture([string]$name, [hashtable]$over, [string[]]$remove) {
    $dir = Join-Path $fixtures "thermalcli-$name"
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    $all = New-Object System.Collections.Generic.List[string]
    foreach ($k in $files.Keys) { $all.Add($k) }
    foreach ($k in $over.Keys) { if (-not $all.Contains($k)) { $all.Add($k) } }
    foreach ($rel in $all) {
        if ($remove -contains $rel) { continue }
        $text = if ($over.ContainsKey($rel)) { $over[$rel] } else { $files[$rel] }
        $path = Join-Path $dir ($rel.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
        [IO.File]::WriteAllText($path, ($text.Replace("`r`n", "`n").TrimEnd() + "`n"), (New-Object Text.UTF8Encoding $false))
    }
}
function Edit([string]$rel, [string]$find, [string]$replace) {
    $t = $files[$rel]
    if (([regex]::Matches($t, [regex]::Escape($find))).Count -lt 1) { throw "no match in $rel for: $find" }
    return @{ $rel = $t.Replace($find, $replace) }
}
function Insert([string]$rel, [string]$line) {
    $t = $files[$rel]
    $j = $t.LastIndexOf("}`n} // namespace x")
    return @{ $rel = $t.Substring(0, $j) + "    $line`n" + $t.Substring($j) }
}
function InsertTop([string]$rel, [string]$line) {
    $t = $files[$rel]
    $j = $t.IndexOf("namespace x")
    return @{ $rel = $t.Substring(0, $j) + "$line`n" + $t.Substring($j) }
}

$R = 'apps/bettercad_cli/ThermalReports.cpp'
$E = 'apps/bettercad_cli/ThermalEdits.cpp'
$O = 'apps/bettercad_cli/ThermalOutput.hpp'
$U = 'src/thermal/ThermalAnalysisRun.cpp'
$H = 'include/bettercad/thermal/ThermalAnalysisRun.hpp'

$cases = [ordered]@{}
function Case([string]$name, $over, [string]$phrase, [string[]]$remove = @()) { $script:cases[$name] = @{ over = $over; phrase = $phrase; remove = $remove } }
Case 'valid' @{} $null
# Rule 1
Case 'includes-solver' (InsertTop $R '#include <bettercad/thermal/ThermalSolve.hpp>') 'includes bettercad/thermal/ThermalSolve.hpp, a header of the derived chain'
Case 'includes-system' (InsertTop $E '#include <bettercad/thermal/ThermalSystem.hpp>') 'includes bettercad/thermal/ThermalSystem.hpp, a header of the derived chain'
Case 'includes-linalg' (InsertTop $R '#include <bettercad/linalg/SparseSolver.hpp>') 'includes bettercad/linalg/SparseSolver.hpp, a header of the derived chain'
Case 'includes-eigen' (InsertTop $R '#include <Eigen/Dense>') 'includes Eigen/Dense, a header of the derived chain'
Case 'includes-renderer' (InsertTop $R '#include <bettercad/renderer/ThermalContour.hpp>') 'includes bettercad/renderer/ThermalContour.hpp, a header of the derived chain'
Case 'includes-structural' (InsertTop $E '#include <bettercad/structural/StructuralPost.hpp>') 'includes bettercad/structural/StructuralPost.hpp, a header of the derived chain'
Case 'includes-other-thermal' (InsertTop $R '#include <bettercad/thermal/ThermalResult.hpp>') 'includes bettercad/thermal/ThermalResult.hpp, a thermal header other than'
Case 'includes-lookalike' (InsertTop $R '#include <bettercad/thermal/xThermalAnalysisRun.hpp>') 'includes bettercad/thermal/xThermalAnalysisRun.hpp, a thermal header other than'
Case 'spaced-include' (InsertTop $R '#  include   < bettercad/thermal/ThermalTransient.hpp >') 'includes bettercad/thermal/ThermalTransient.hpp, a header of the derived chain'
# Rule 2
Case 'names-solve' (Insert $R 'solveSteadyThermal(a);') 'names a link of the derived chain (solveSteady)'
Case 'names-assemble' (Insert $R 'assembleThermalSystem(a);') 'names a link of the derived chain (assembleThermal)'
Case 'names-validate' (Insert $R 'validateThermalAnalysis(a);') 'names a link of the derived chain (validateThermalAnalysis)'
Case 'names-gradient' (Insert $R 'g.gradientFrom(te);') 'names a link of the derived chain (gradientFrom)'
Case 'names-time-grid' (Insert $R 'TimeGrid grid;') 'names a link of the derived chain (TimeGrid)'
# Rule 3
Case 'material-property' (Insert $E 'double k = thermalConductivity;') 'names a material property (Conductivity)'
Case 'material-density' (Insert $E 'auto rho = density;') 'names a material property (density)'
# Rule 4
Case 'float-tolerance' (Insert $E 'double tolerance = 1e-9;') 'holds a floating-point literal'
Case 'float-default' (Insert $E 'auto ambient = 293.15;') 'holds a floating-point literal'
Case 'float-step' (Insert $R 'auto dt = 0.5;') 'holds a floating-point literal'
Case 'float-no-leading-digit' (Insert $R 'const double h = .5;') 'holds a floating-point literal'
Case 'float-after-char-literal' (Insert $R "char a = '`"'; double t = 1.5e-6; char b = '`"';") 'holds a floating-point literal'
Case 'float-after-slashes-in-string' (Insert $R 'const char* u = "//"; double t = 1.5e-6;') 'holds a floating-point literal'
Case 'float-in-a-new-file' @{ 'apps/bettercad_cli/ThermalExtra.cpp' = "namespace x {`ndouble t = 1.5;`n}`n" } 'ThermalExtra.cpp: holds a floating-point literal'
# Rule 5
Case 'decision-residual' (Insert $R 'bool ok = normalizedResidual() < limit;') 'decides on a residual, a balance or a status'
Case 'decision-reversed' (Insert $R 'bool ok = limit > normalizedResidual();') 'decides on a residual, a balance or a status'
Case 'decision-balance' (Insert $R 'if (balance.balanced() == true) {}') 'decides on a residual, a balance or a status'
Case 'decision-balanced-branch' (Insert $R 'if (!balance.balanced()) { fail(); }') 'decides on a residual, a balance or a status'
Case 'decision-status' (Insert $R 'if (r.status() == ValidationStatus::Pass) {}') 'decides on a residual, a balance or a status'
Case 'decision-enumerator' (Insert $R 'auto s = CheckStatus::Warning;') 'decides on a residual, a balance or a status'
# Rule 6
Case 'correction-abs' (Insert $E 'double a = std::abs(flux);') 'corrects or rounds a value (abs()'
Case 'correction-clamp' (Insert $E 'auto d = std::clamp(dt, lo, hi);') 'corrects or rounds a value (std::clamp)'
Case 'correction-max' (Insert $R 'auto m = std::max(a, b);') 'corrects or rounds a value (std::max)'
Case 'math-sqrt' (Insert $R 'auto r = std::sqrt(v);') 'calls a math function (std::sqrt()'
Case 'math-pow' (Insert $E 'auto r = std::pow(v, e);') 'calls a math function (std::pow()'
# Rule 7
Case 'index-node' (Insert $R 'auto t = temperatures[node.value()];') 'as an index or parks it in a local'
Case 'index-element' (Insert $R 'auto q = fluxes[element.value() - 1];') 'as an index or parks it in a local'
Case 'index-through-a-local' (Insert $R 'const auto raw = node->value();') 'as an index or parks it in a local'
Case 'option-node' (Insert $E 'parseArguments(args, {{"--node", true}});') 'accepts a mesh-local target option ("--node")'
Case 'option-facet' (Insert $E 'parseArguments(args, {{"--facet", true}});') 'accepts a mesh-local target option ("--facet")'
Case 'option-dof' (Insert $E 'parseArguments(args, {{"--dof", true}});') 'accepts a mesh-local target option ("--dof")'
# Rule 8
Case 'persist-ofstream' (Insert $R 'std::ofstream file;') 'writes a file or parses document JSON itself (ofstream)'
Case 'persist-save' (Insert $E 'saveDocument(d, p);') 'writes a file or parses document JSON itself (saveDocument)'
Case 'persist-json' (Insert $R 'nlohmann::ordered_json j;') 'writes a file or parses document JSON itself (nlohmann)'
# Rule 9
Case 'unit-celsius' (Insert $E 'double kelvin = celsius + offset;') 'converts a temperature unit (celsius)'
Case 'unit-degc' (Insert $E 'auto v = kelvin_from_degC(t);') 'converts a temperature unit (degC)'
# Rule 10
Case 'stream-cout' (Insert $R 'std::cout << x;') 'writes to a process stream directly (std::cout)'
Case 'stream-printf' (Insert $E 'printf(fmt);') 'writes to a process stream directly (printf)'
Case 'stream-println' (Insert $R 'std::println("x");') 'writes to a process stream directly (std::print)'
Case 'stream-write' (Insert $R 'sink.write(data, n);') 'writes to a process stream directly (.write('
Case 'stream-extra-out' (Insert $R 'out << banner;') '3 statement(s) write to out, expected exactly 2'
Case 'stream-err' (Insert $R 'err << banner;') '1 statement(s) write to err directly'
# Rule 11
Case 'raw-number' (Insert $R 'JsonValue::number(d);') 'JsonValue::number( appears 1 time(s); a double must go through FiniteJson'
Case 'raw-quantity' (Insert $R 'JsonValue::quantity(v, u);') 'JsonValue::quantity( appears'
# Rule 12
Case 'verdict-lost' (Edit $R "    run.accepted();`n    run.accepted();`n    run.accepted();`n" "    run.accepted();`n    run.accepted();`n") 'uses accepted() 4 time(s), expected exactly 5'
Case 'verdict-extra' (Insert $R 'run.accepted();') 'uses accepted() 6 time(s), expected exactly 5'
Case 'verdict-return-weakened' (Edit $R "    return run.accepted() ? ExitCode::Success : ExitCode::Failure;`n    return run.accepted() ? ExitCode::Success : ExitCode::Failure;" "    return run.accepted() ? ExitCode::Success : ExitCode::Failure;`n    return (run.accepted(), ExitCode::Success);") '1 exit statement(s) are exactly'
Case 'run-entry-lost' (Edit $R "    runThermalAnalysis(d, r, m, a);`n" "") 'uses runThermalAnalysis 0 time(s), fewer than the 1'
Case 'edits-remove-lost' (Edit $E "    thermal::RemoveThermalLoadCommand b;`n" "") 'uses RemoveThermalLoadCommand 0 time(s), fewer than the 1'
Case 'edits-execute-lost' (Edit $E "    executeEdit(e6);`n" "") 'uses executeEdit( 5 time(s), fewer than the 6'
Case 'edits-id-cap-lost' (Edit $E "    constexpr auto kLargestFileId = 1;`n    if (v > kLargestFileId) {}`n" "") 'uses kLargestFileId 0 time(s), fewer than the 2'
Case 'nonfinite-check-lost' (Edit $R "    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n" "    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n    fin.sawNonFinite();`n") 'uses sawNonFinite() 5 time(s), expected exactly 6'
Case 'refusal-lost' (Edit $R "    refusalOf(r3);`n" "") 'uses refusalOf 2 time(s), expected exactly 3'
Case 'node-mapping-lost' (Edit $R "    map.nodeOf(i);`n" "") 'uses nodeOf 0 time(s), fewer than the 1'
Case 'ordinal-lost' (Edit $R "    ordinalOrThrow(post, e3);`n" "") 'uses ordinalOrThrow 2 time(s), fewer than the 3'
Case 'id-from-position' (Insert $R 'g(static_cast<std::uint64_t>(i) + 1U);') '4 conversion(s) of a value to std::uint64_t but only 3 of them are'
Case 'id-from-position-in-place' (Edit $R 'g(static_cast<std::uint64_t>(node->value()));' 'g(static_cast<std::uint64_t>(index) + 1U);') '3 conversion(s) of a value to std::uint64_t but only 2 of them are'
Case 'ordinal-is-id-minus-one' (Edit $R "    return *ordinal;`n" "    return id - 1U;`n") 'the ordinal lookup lost its answer or its refusal (0 `return *ordinal;`, 2 defect throw(s)'
Case 'ordinal-fallback-not-thrown' (Edit $R "        throw std::logic_error(`"the node is not in the numbering`");`n" "        return 0U;`n") 'the ordinal lookup lost its answer or its refusal (1 `return *ordinal;`, 1 defect throw(s)'
Case 'reports-missing' @{}'apps/bettercad_cli/ThermalReports.cpp: is missing' @($R)
Case 'edits-missing' @{} 'apps/bettercad_cli/ThermalEdits.cpp: is missing' @($E)
Case 'output-missing' @{} 'apps/bettercad_cli/ThermalOutput.hpp: is missing' @($O)
# Rule 13
Case 'arith-balance-sum' (Insert $R 'double r = state.terms.a.si() + state.terms.b.si();') 'does arithmetic on a quantity or parks one in a local'
Case 'arith-flux-scale' (Insert $R 'double q = k * flux.si();') 'does arithmetic on a quantity or parks one in a local'
Case 'arith-unit-rescale' (Insert $E 'double v = temperature.si() / scale;') 'does arithmetic on a quantity or parks one in a local'
Case 'arith-through-locals' (Insert $R 'const double a = x.si(); const double b = y.si(); const double s = a + b;') 'does arithmetic on a quantity or parks one in a local'
Case 'arith-on-an-arrow' (Insert $R 'g(held->value.si() * scale);') 'does arithmetic on a quantity or parks one in a local'
Case 'arith-across-a-line' (Insert $R "g(a.si()`n        + b.si());") 'does arithmetic on a quantity or parks one in a local'
# Rule 14
Case 'macro-define' (InsertTop $R '#define PASTE(a, b) a') 'defines a macro or pastes tokens'
Case 'macro-paste' (Insert $R 'auto p = left##right;') 'defines a macro or pastes tokens'
# Rule 15
Case 'output-isfinite-lost' (Edit $O 'if (!std::isfinite(v)) {' 'if (false) {') 'lost std::isfinite'
Case 'output-tiebreak-lost' (Edit $O 'if (candidate.id < e.low.id) {}' 'if (candidate.id > e.low.id) {}') 'lost candidate.id < e.low.id'
Case 'output-tiebreak-high-lost' (Edit $O 'if (candidate.id < e.high.id) {}' 'if (candidate.id > e.high.id) {}') 'lost candidate.id < e.high.id'
Case 'output-valuefinite-lost' (Edit $O 'entry.set("valueFinite", JsonValue::boolean(ok));' 'entry.set("fine", JsonValue::boolean(ok));') 'lost valueFinite'
# Rule 16
Case 'run-link-missing' (Edit $U "    assembleThermalSystem(a);`n" "") 'assembleThermalSystem is called 0 time(s)'
Case 'run-link-twice' (Insert $U 'solveSteadyThermal(b);') 'solveSteadyThermal is called 2 time(s)'
Case 'run-order' (Edit $U "    prepareThermalLoads(a);`n    prepareThermalBoundary(a);`n" "    prepareThermalBoundary(a);`n    prepareThermalLoads(a);`n") 'prepareThermalBoundary is called before prepareThermalLoads'
Case 'run-validate-twice' (Insert $U 'validateThermalAnalysis(document, regenerator, mesher, analysis, run.derivedState());') 'validateThermalAnalysis is called 2 time(s)'
Case 'run-validate-blind' (Edit $U 'run.derivedState()' '{}') 'is not handed derivedState()'
Case 'run-missing' @{} 'src/thermal/ThermalAnalysisRun.cpp: is missing' @($U)
# Rule 17
Case 'run-float' (Insert $U 'double x = 0.5;') 'holds a floating-point literal'
Case 'run-math' (Insert $U 'auto r = std::sqrt(x);') 'calls a math function (std::sqrt()'
Case 'run-tolerance' (Insert $U 'auto t = settings.relativeResidualTolerance;') 'holds a tolerance, a status or severity decision'
Case 'run-status' (Insert $U 'if (report.status() == x) {}') 'holds a tolerance, a status or severity decision'
Case 'run-assigns-definition' (Insert $U 'definition.solver = other;') 'holds a tolerance, a status or severity decision'
Case 'run-builds-settings' (Insert $U 'ThermalSolverSettings s{};') 'holds a tolerance, a status or severity decision'
Case 'run-clears-the-report' (Insert $U 'run.validation.findings.clear();') 'holds a tolerance, a status or severity decision'
# Rule 18
Case 'run-meshes' (Insert $U 'mesher.generate(document);') 'meshes or writes (generate'
Case 'run-writes' (Insert $U 'document.addObject(o);') 'meshes or writes (addObject)'
Case 'run-edits-definition' (Insert $U 'object->setDefinition(d);') 'meshes or writes (setDefinition)'
# Rule 19
Case 'header-solver-success' (Edit $H '{ return !stoppedAt.has_value() && validation.accepted(); }' '{ return !stoppedAt.has_value(); }') 'accepted() is not exactly'
Case 'header-validator-only' (Edit $H '{ return !stoppedAt.has_value() && validation.accepted(); }' '{ return validation.accepted(); }') 'accepted() is not exactly'
Case 'header-missing' @{} 'include/bettercad/thermal/ThermalAnalysisRun.hpp: is missing' @($H)

$report = New-Object System.Collections.Generic.List[string]
$bad = 0
foreach ($name in $cases.Keys) {
    $c = $cases[$name]
    Write-Fixture $name $c.over $c.remove
    $out = (& cmake "-DSOURCE_DIR=$($fixtures.Replace('\','/'))/thermalcli-$name" -P $gate 2>&1 | Out-String)
    $code = $LASTEXITCODE
    $flat = $out -replace '\s+', ' '
    $one = $out -match 'failed: 1 violation\(s\)'
    $frag = if ($c.phrase) { $flat.Contains($c.phrase) } else { $true }
    $ok = if ($name -eq 'valid') { $code -eq 0 } else { $code -ne 0 -and $one -and $frag }
    if (-not $ok) { $bad++; $report.Add(("{0,-30} BAD code={1} oneViolation={2} phrase={3}`n    {4}" -f $name, $code, $one, $frag, $flat.Substring(0, [Math]::Min(500, $flat.Length)))) }
}
$report | ForEach-Object { $_ }
"fixtures: $($cases.Count)   bad: $bad"
if ($bad -ne 0) { exit 1 }

# ---- the CMake block
function ToRegex([string]$phrase) {
    $e = [regex]::Escape($phrase).Replace('\ ', ' ').Replace('\#', '#')
    $e = $e.Replace('\', '\\').Replace('"', '\"').Replace(' ', '[ \n]+')
    return $e
}
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('# P18-CLI-001: the thermal command line is an adapter over the qualified chain, and the chain''s order lives in one place. The check')
[void]$sb.AppendLine('# scans every apps/bettercad_cli/Thermal*.cpp and .hpp -- literals masked in one pass, comments removed -- for a header of the derived')
[void]$sb.AppendLine('# chain, a link of it, a material property, a floating-point literal, a decision on a residual or a status written either way round,')
[void]$sb.AppendLine('# a correction or a math function, a mesh id used as an index or accepted as a target, a file write, a unit conversion, a direct write')
[void]$sb.AppendLine('# to a process stream, a macro, arithmetic on a quantity (also through a local), and counts the owners each file must use EXACTLY')
[void]$sb.AppendLine('# where a slack would hide a deleted verdict. It scans src/thermal/ThermalAnalysisRun.cpp for the chain, once each, in order, and for')
[void]$sb.AppendLine('# any arithmetic, policy, meshing or write; and the header for the exact spelling of `accepted()`. Each fixture tree holds exactly one')
[void]$sb.AppendLine('# defect (or none); the valid tree also holds comments and strings that name forbidden things, which are not code.')
[void]$sb.AppendLine('set(_check_thermalcli "${CMAKE_CURRENT_SOURCE_DIR}/CheckThermalCliOwnership.cmake")')
[void]$sb.AppendLine('add_test(NAME architecture.thermalcli-ownership')
[void]$sb.AppendLine('    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${_check_thermalcli}")')
[void]$sb.AppendLine('bettercad_add_process_test(architecture.thermalcli-ownership.valid')
[void]$sb.AppendLine('    COMMAND "${CMAKE_COMMAND}" ARGS "-DSOURCE_DIR=${_fixtures}/thermalcli-valid|-P|${_check_thermalcli}"')
[void]$sb.AppendLine('    EXIT_CODE 0 STDOUT_REGEX "Thermal CLI ownership check passed")')
foreach ($name in $cases.Keys) {
    if ($name -eq 'valid') { continue }
    $rx = ToRegex $cases[$name].phrase
    [void]$sb.AppendLine("bettercad_add_process_test(architecture.thermalcli-ownership.$name")
    [void]$sb.AppendLine("    COMMAND `"`${CMAKE_COMMAND}`" ARGS `"-DSOURCE_DIR=`${_fixtures}/thermalcli-$name|-P|`${_check_thermalcli}`"")
    [void]$sb.AppendLine("    EXIT_CODE 1 STDERR_REGEX `"check failed: 1 violation\\(s\\):.*$rx`")")
}
$existing = [IO.File]::ReadAllText($cmakeLists)
$marker = "# P18-CLI-001: the thermal command line is an adapter"
$at = $existing.IndexOf($marker)
if ($at -ge 0) { $existing = $existing.Substring(0, $at).TrimEnd() + "`n`n" } else { $existing = $existing.TrimEnd() + "`n`n" }
[IO.File]::WriteAllText($cmakeLists, $existing + $sb.ToString(), (New-Object Text.UTF8Encoding $false))
# the directory names that no longer correspond to a case are removed
Get-ChildItem $fixtures -Directory -Filter 'thermalcli-*' | Where-Object { -not $cases.Contains($_.Name.Substring('thermalcli-'.Length)) } | ForEach-Object { Remove-Item $_.FullName -Recurse -Force; "removed stale fixture $($_.Name)" }
"registered $($cases.Count) trees ($($cases.Count - 1) defects)"
