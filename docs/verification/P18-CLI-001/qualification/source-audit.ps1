# P18-CLI-001 source audit: textual facts about the thermal CLI files and the entry that the ownership gate and the tests rely on, printed so that a reviewer
# can read them without running anything. Comments and string literals are removed before searching (a comment may name what is forbidden, and so may a help
# string); the numbers below are of CODE.
param([string]$Out = "source-audit.txt")
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = @("apps/bettercad_cli/ThermalReports.cpp", "apps/bettercad_cli/ThermalEdits.cpp", "apps/bettercad_cli/ThermalOutput.hpp",
           "src/thermal/ThermalAnalysisRun.cpp", "include/bettercad/thermal/ThermalAnalysisRun.hpp")
function Strip([string]$text) {
    $text = [regex]::Replace($text, '"([^"\\\r\n]|\\.)*"|''([^''\\\r\n]|\\.)+''', '""')
    $text = [regex]::Replace($text, '//[^\n]*', '')
    return [regex]::Replace($text, '(?m)^\s*#\s*include[^\n]*', '')
}
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("P18-CLI-001 source audit")
$checks = [ordered]@{
    "Tet4 / element matrices (Ke, Ce, gradientFrom, computeTet4Geometry)" = 'computeTet4Geometry|gradientFrom|Tet4Geometry|\bKe\b|\bCe\b'
    "load / boundary integration (prepareThermal*, ThermalLoadContribution)" = 'prepareThermal|ThermalLoadContribution|PreparedThermal'
    "Robin / convection matrices"                                          = 'ConvectionBoundary[ \t]*\{|PreparedConvection|Kconv|Qconv'
    "assembly (assembleThermalSystem, ConductanceMatrix, CapacityMatrix)"  = 'assembleThermal|ConductanceMatrix|CapacityMatrix|ThermalHeatVector'
    "steady solve (solveSteady*, factorisation, anchoring)"                = 'solveSteady|SteadyThermalSolution|analyseAnchoring|Cholesky|LDLT|SparseMatrix|Eigen'
    "transient integration (solveTransient*, TimeGrid, backward Euler)"    = 'solveTransient|TransientThermalSolution|TimeGrid|[Bb]ackward[ ]?Euler'
    "recovery (recover*Thermal, ThermalPostResult construction)"           = 'recoverSteady|recoverTransient|recoverThermal'
    "balance (recover*Balance, steadyHeatBalance, balanceWithinTolerance)" = 'recoverSteadyBalance|recoverTransientBalance|steadyHeatBalance|transientStepHeatBalance|balanceWithinTolerance'
    "validation policy (validateThermalAnalysis, thresholds, severities)"  = 'validateThermalAnalysis|thermalQualityThresholds|kThermal|kBalance|ValidationSeverity::(Warning|Info)|ValidationStatus::'
    "material properties (conductivity, density, specific heat)"           = '[Cc]onductivity|[Dd]ensity|[Ss]pecificHeat|specific_heat'
    "floating-point literals"                                              = '[0-9]\.[0-9]|[0-9][eE][-+]?[0-9]|([^A-Za-z0-9_)]|^)\.[0-9]'
    "abs / clamp / min / max / round"                                      = 'abs\(|std::clamp|std::max|std::min|std::round|std::floor|std::ceil'
    "sqrt / pow / exp / log"                                               = 'std::(sqrt|pow|exp|log)\(|[^A-Za-z_](sqrt|pow|exp|log)[ \t]*\('
    "Celsius / Fahrenheit / degC / 273.15"                                 = '[Cc]elsius|[Ff]ahrenheit|degC|degF|273\.15'
    "ValidationStatus / CheckStatus / BalanceStatus enumerators"           = 'ValidationStatus::|CheckStatus::|BalanceStatus::'
    "id value() used as an index"                                          = '\[[^\]]*(\.|->)value\(\)[^\]]*\]'
    "file writes (ofstream, saveDocument, writeFile)"                      = 'ofstream|fstream|fopen|saveDocument|writeFile'
    "process streams (std::cout, printf, puts, std::print)"                = 'std::cout|std::cerr|printf|std::print|[^a-z_]puts[ \t]*\('
    "meshing / writing in the entry (generate, modifyObject, addObject)"   = 'generate[ \t]*\(|modifyObject|addObject|removeObject|setDefinition'
}
foreach ($f in $files) {
    $raw = [IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))")
    $code = Strip $raw
    $lines.Add("")
    $lines.Add("== $f ($((($raw -split "`n").Count)) lines, $($raw.Length) bytes)")
    foreach ($name in $checks.Keys) {
        $n = ([regex]::Matches($code, $checks[$name])).Count
        $lines.Add(("  {0,-72} {1}" -f $name, $n))
    }
}
$lines.Add("")
$lines.Add("== owners used (each at least as many times as there are places that need it)")
$reports = Strip ([IO.File]::ReadAllText("$root\apps\bettercad_cli\ThermalReports.cpp"))
$edits = Strip ([IO.File]::ReadAllText("$root\apps\bettercad_cli\ThermalEdits.cpp"))
$owners = [ordered]@{
    "reports: runThermalAnalysis" = @($reports, 'runThermalAnalysis', 1); "reports: accepted()" = @($reports, 'accepted\(\)', 5)
    "reports: thermalAnalysisIds" = @($reports, 'thermalAnalysisIds', 1); "reports: checkFaceName" = @($reports, 'checkFaceName', 4)
    "reports: nodeOf" = @($reports, 'nodeOf', 1); "reports: ordinalOrThrow" = @($reports, 'ordinalOrThrow', 3)
    "reports: validationJson" = @($reports, 'validationJson', 3); "reports: sawNonFinite()" = @($reports, 'sawNonFinite\(\)', 6)
    "edits: AddThermalLoadCommand" = @($edits, 'AddThermalLoadCommand', 1); "edits: RemoveThermalLoadCommand" = @($edits, 'RemoveThermalLoadCommand', 1)
    "edits: AddThermalBoundaryConditionCommand" = @($edits, 'AddThermalBoundaryConditionCommand', 1)
    "edits: RemoveThermalBoundaryConditionCommand" = @($edits, 'RemoveThermalBoundaryConditionCommand', 1)
    "edits: CreateThermalAnalysisCommand" = @($edits, 'CreateThermalAnalysisCommand', 1)
    "edits: parseFaceReference" = @($edits, 'parseFaceReference', 2); "edits: parseQuantity" = @($edits, 'parseQuantity', 1)
    "edits: executeEdit(" = @($edits, 'executeEdit\(', 6); "edits: kLargestFileId" = @($edits, 'kLargestFileId', 2)
}
foreach ($n in $owners.Keys) {
    $c = ([regex]::Matches($owners[$n][0], $owners[$n][1])).Count
    $lines.Add(("  {0,-52} {1,3}   (at least {2})  {3}" -f $n, $c, $owners[$n][2], $(if ($c -ge $owners[$n][2]) { "ok" } else { "TOO FEW" })))
}
$lines.Add("")
$lines.Add("== includes of the thermal files")
foreach ($f in $files) {
    foreach ($m in [regex]::Matches([IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))"), '(?m)^#\s*include\s+[<"][^>"]+[>"]')) { $lines.Add("  ${f}: $($m.Value)") }
}
$lines.Add("")
$lines.Add("== the dispatch: which core API each thermal verb reaches")
$lines.Add("  reports   -> prepare(): io::loadDocument, Regenerator::regenerate, thermal::thermalAnalysisIds")
$lines.Add("  reports   -> analyse(): Mesher::generate, thermal::runThermalAnalysis")
$lines.Add("  edits     -> executeEdit(): the P18-CMD command objects only (Create/Add/Remove)")
$lines.Add("  verdict   -> ThermalAnalysisRun::accepted()")
[IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Get-Content $Out
