# P18-VALID-001 source audit: textual facts about the validator that the ownership gate and the tests rely on, printed so that a reviewer can
# read them without running anything. Comments are removed before searching (a comment may name what is forbidden).
param([string]$Out = "source-audit.txt")
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = "src/thermal/ThermalValidation.cpp", "include/bettercad/thermal/ThermalValidation.hpp"
function Strip([string]$text) {
    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($text, '//[^\n]*', '')
}
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("P18-VALID-001 source audit")
foreach ($f in $files) {
    $raw = [IO.File]::ReadAllText("$root\$($f.Replace('/', '\'))")
    $code = Strip $raw
    $lines.Add("")
    $lines.Add("== $f ($((($raw -split "`n").Count)) lines, $($raw.Length) bytes)")
    $checks = [ordered]@{
        "unordered containers / std::hash"       = 'unordered_|std::hash'
        "clock, time, random"                    = 'chrono|std::time|clock\(|rand\(|random_device|mt19937'
        "threads / atomics / mutexes"            = 'std::thread|std::async|std::mutex|std::atomic|std::jthread'
        "mutable globals (static, non-const)"    = '(^|\s)static\s+(?!constexpr|const\b|inline constexpr)[A-Za-z_:<>]+\s+[a-z][A-Za-z_]*\s*(=|;|\{)'
        "tolerance literals 1e-9 .. 1e-16"       = '\b1(\.0*)?[eE]-(9|1[0-6])\b'
        "calls that would solve or assemble"     = 'solveSteadyThermal|solveTransientThermal|assembleThermalSystem|factor[A-Za-z]*\(|backSubstitute|recoverSteadyThermal|recoverTransientThermal|recoverSteadyBalance|recoverTransientBalance'
        "calls that would remesh or regenerate"  = '\.generate\(|regenerateAll|assignMaterial|setMaterial'
        "mesh metric computation"                = 'evaluateTetQuality|evaluateMeshQuality'
        "raw owning memory"                      = '\bnew\b|\bdelete\b|malloc|free\('
        "goto / setjmp / exceptions thrown"      = '\bgoto\b|setjmp|\bthrow\b'
    }
    foreach ($name in $checks.Keys) {
        $n = ([regex]::Matches($code, $checks[$name])).Count
        $lines.Add(("  {0,-42} {1}" -f $name, $n))
    }
    $lines.Add(("  {0,-42} {1}" -f "BETTERCAD_THERMAL_EXPORT declarations", ([regex]::Matches($code, 'BETTERCAD_THERMAL_EXPORT')).Count))
}
$lines.Add("")
$lines.Add("== authorities named in ThermalValidation.cpp (each must be present)")
$cpp = Strip ([IO.File]::ReadAllText("$root\src\thermal\ThermalValidation.cpp"))
foreach ($n in "validateSteadySolution", "validateTransientStep", "balanceWithinTolerance", "analyseAnchoring", "requireCurrent", "requireThermalModel", "thermalInputProblem", "staleReasons", "summaries", "direction", "thermalMaterialProblem", "thermalLoadProblem", "thermalBoundaryFailure", "reduceThermalSystem") {
    $lines.Add(("  {0,-34} {1}" -f $n, ([regex]::Matches($cpp, "\b$n\b")).Count))
}
$lines.Add("")
$lines.Add("== validation codes")
$hdr = Get-Content "$root\include\bettercad\thermal\ThermalValidation.hpp"
$start = ($hdr | Select-String 'enum class ValidationCode').LineNumber
$codes = @()
for ($i = $start; $i -lt $hdr.Count; $i++) {
    $l = $hdr[$i].Trim()
    if ($l -eq '};') { break }
    if ($l -match '^([A-Z][A-Za-z0-9]+),$') { $codes += $Matches[1] }
}
$tests = Get-ChildItem "$root\tests" -Recurse -File | Where-Object { $_.Name -match '^ThermalValidation' }
$testText = ($tests | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$lines.Add("  codes in the enumeration: $($codes.Count)")
$untested = @()
foreach ($c in $codes) {
    if (([regex]::Matches($testText, "ValidationCode::$c\b")).Count -lt 1) { $untested += $c }
}
$lines.Add(("  codes never named by a test: $($untested.Count)" + $(if ($untested.Count) { " " + ($untested -join " ") } else { "" })))
[IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Get-Content $Out
