# P18-VIZ-001 source audit: textual facts about the thermal visualization layer that the ownership gate and the tests rely on, printed so that a
# reviewer can read them without running anything. Comments are removed before searching (a comment may name what is forbidden).
param([string]$Out = "source-audit.txt")
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = "src/renderer/ThermalResultView.cpp", "src/renderer/ThermalContour.cpp", "src/renderer/ThermalInspection.cpp", "src/renderer/ThermalOverlay.cpp",
    "include/bettercad/renderer/ThermalResultView.hpp", "include/bettercad/renderer/ThermalContour.hpp", "include/bettercad/renderer/ThermalInspection.hpp", "include/bettercad/renderer/ThermalOverlay.hpp"
function Strip([string]$text) {
    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($text, '//[^\n]*', '')
}
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("P18-VIZ-001 source audit")
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
        "unit literals 1000 / 1e3 / 0.001 / 1e-3"  = '(?<![0-9.eE_])(1000(\.0*)?|1e3|1\.0e3|0\.001|1e-3|1\.0e-3)(?![0-9eE_])'
        "Celsius / 273.15"                        = '[Cc]elsius|273\.15'
        "toModel (the one conversion is elsewhere)" = 'toModel'
        "gradient recovery / Fourier / balance"   = 'gradientFrom|recoverThermalElementFields|recoverSteady|recoverTransient|[Ff]ourier|steadyHeatBalance|balanceSteadySolution|balanceTransientSolution'
        "solve / assemble / integrate"            = 'solveSteadyThermal|solveTransientThermal|solveSymmetricSparse|assembleThermalSystem|integrateConvection|integrateSurfaceHeatFlux|integrateVolumetricHeatSource|computeTet4'
        "validation rule seams"                   = 'validateThermalAnalysis|checkSteadyResidual|checkTransientResiduals|checkHeatBalanceStates|checkPostStates|classifyThermalMesh|balanceWithinTolerance'
        "raw id as an index"                      = 'value\(\)[ \t]*(\]|[-+][ \t]*[A-Za-z0-9_]+[ \t]*\])'
        "raw owning memory"                       = '\bnew\b|\bdelete\b|malloc|free\('
        "goto / setjmp / exceptions thrown"       = '\bgoto\b|setjmp|\bthrow\b'
        "hypot / sqrt (norms)"                    = 'hypot|sqrt'
    }
    foreach ($name in $checks.Keys) {
        $n = ([regex]::Matches($code, $checks[$name])).Count
        $lines.Add(("  {0,-44} {1}" -f $name, $n))
        if (-not $totals.Contains($name)) { $totals[$name] = 0 }
        $totals[$name] += $n
    }
    $lines.Add(("  {0,-44} {1}" -f "BETTERCAD_RENDERER_EXPORT declarations", ([regex]::Matches($code, 'BETTERCAD_RENDERER_EXPORT')).Count))
}
$lines.Add("")
$lines.Add("== totals over the eight files (every forbidden pattern must be 0 except the two documented norms)")
foreach ($name in $totals.Keys) { $lines.Add(("  {0,-44} {1}" -f $name, $totals[$name])) }
$lines.Add("")
$lines.Add("== owners named (each must be present in the file that must read it)")
$required = [ordered]@{
    "src/renderer/ThermalInspection.cpp" = "isCurrent", "staleReasons", "requireThermalModel", "postSourceOf", "ordinalOf", "dofOf", "currentSteadySolutionSource"
    "src/renderer/ThermalResultView.cpp" = "describes", "elementIds", "descriptorOf"
    "src/renderer/ThermalOverlay.cpp"    = "describes", "elementIds", "ordinalOf", "sourceOfFacet", "classifyBoundaryCardinality", "highlightFor"
    "src/renderer/ThermalContour.cpp"    = "nodeOfVertex", "elementOfTriangle", "describes"
}
foreach ($f in $required.Keys) {
    $code = Strip ([IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))"))
    foreach ($n in $required[$f]) {
        $lines.Add(("  {0,-40} {1,-30} {2}" -f $f, $n, ([regex]::Matches($code, "\b$n\b")).Count))
    }
}
$lines.Add("")
$lines.Add("== the two display-norm sites (the only hypot calls)")
foreach ($f in "src/renderer/ThermalResultView.cpp", "src/renderer/ThermalOverlay.cpp") {
    $code = Strip ([IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))"))
    $lines.Add(("  {0,-40} hypot {1}" -f $f, ([regex]::Matches($code, 'hypot')).Count))
}
[IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Get-Content $Out
