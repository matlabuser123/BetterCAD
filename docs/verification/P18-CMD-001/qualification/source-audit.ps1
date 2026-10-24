# P18-CMD-001 source audit: textual facts about the thermal command files that the ownership gate and the tests rely on, printed so that a reviewer
# can read them without running anything. Comments are removed before searching (a comment may name what is forbidden).
param([string]$Out = "source-audit.txt")
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = "src/thermal/ThermalCommands.cpp", "include/bettercad/thermal/ThermalCommands.hpp"
function Strip([string]$text) {
    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($text, '//[^\n]*', '')
}
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("P18-CMD-001 source audit")
$totals = [ordered]@{}
foreach ($f in $files) {
    $raw = [IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))")
    $code = Strip $raw
    $lines.Add("")
    $lines.Add("== $f ($((($raw -split "`n").Count)) lines, $($raw.Length) bytes)")
    $checks = [ordered]@{
        "unordered containers / std::hash"        = 'unordered_|std::hash'
        "clock, time, random"                     = 'chrono|std::time|clock\(|rand\(|random_device|mt19937'
        "threads / atomics / mutexes"             = 'std::thread|std::async|std::mutex|std::atomic|std::jthread'
        "const_cast / mutable"                    = 'const_cast|\bmutable\b'
        "mutable globals (static, non-const)"     = '(^|\s)static\s+(?!constexpr|const\b|inline constexpr)[A-Za-z_:<>]+\s+[a-z][A-Za-z_]*\s*(=|;|\{)'
        "text round trip (parse a number)"        = 'stod|stof|stoi|stoll|from_chars|strtod|sscanf|istringstream|lexical_cast'
        "mesh identity (NodeId / ElementId / MeshStamp)" = '\bNodeId\b|\bElementId\b|\bMeshStamp\b|\bFacetId\b'
        "derived names (DOF / system / solution / post)" = 'ThermalDof|ThermalSystem|SteadyThermalSolution|TransientThermalSolution|ThermalPostResult|ThermalBalanceResult|ThermalValidationReport|PreparedThermal'
        "solve / assemble / remesh / validate-run" = 'solveSteadyThermal|solveTransientThermal|solveSymmetricSparse|assembleThermalSystem|\bremesh|generateMesh|validateThermalAnalysis'
        "a stale flag of its own"                 = 'isStale|markStale|stale_|staleFlag|\bbool\s+\w*[Ss]tale|invalidate\w*\('
        "positional addressing (loads[i] / begin()+n)" = '(loads|boundaryConditions)\s*\[|\.begin\(\)\s*\+|std::next\(|std::advance\('
        "revision written by hand"                = 'setRevision|bumpRevision|\+\+\s*revision|revision_\s*='
        "raw owning memory"                       = '\bnew\b|\bdelete\b|malloc|free\('
        "goto / setjmp / exceptions thrown"       = '\bgoto\b|setjmp|\bthrow\b'
    }
    foreach ($name in $checks.Keys) {
        $n = ([regex]::Matches($code, $checks[$name])).Count
        $lines.Add(("  {0,-48} {1}" -f $name, $n))
        if (-not $totals.Contains($name)) { $totals[$name] = 0 }
        $totals[$name] += $n
    }
    $lines.Add(("  {0,-48} {1}" -f "BETTERCAD_THERMAL_EXPORT declarations", ([regex]::Matches($code, 'BETTERCAD_THERMAL_EXPORT')).Count))
}
$lines.Add("")
$lines.Add("== totals over the two files (every forbidden pattern must be 0)")
foreach ($name in $totals.Keys) { $lines.Add(("  {0,-48} {1}" -f $name, $totals[$name])) }
$lines.Add("")
$lines.Add("== owners named (each must be present in the command source)")
$required = "modifyObject", "setDefinition", "validate", "findThermalAnalysis", "requireNamedFeature", "divergedFrom", "steadyHasNo"
$code = Strip ([IO.File]::ReadAllText("$root\src\thermal\ThermalCommands.cpp"))
foreach ($n in $required) {
    $lines.Add(("  {0,-48} {1}" -f $n, ([regex]::Matches($code, "\b$n\b")).Count))
}
$hdrCode = Strip ([IO.File]::ReadAllText("$root\include\bettercad\thermal\ThermalCommands.hpp"))
$lines.Add(("  {0,-48} {1}  (the header, which wraps it)" -f "AddObjectCommand", ([regex]::Matches($hdrCode, "\bAddObjectCommand\b")).Count))
$lines.Add("")
$lines.Add("== includes of the two command files")
foreach ($f in $files) {
    $lines.Add("  $f")
    foreach ($m in [regex]::Matches([IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))"), '(?m)^#include\s+[<"][^>"]+[>"]')) { $lines.Add("    " + $m.Value) }
}
[IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Get-Content $Out
