# P18-VALID-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, the validation contract
# fingerprint, the policy fingerprint, the thermal mesh-acceptance fingerprint, the ownership fingerprint and the dependency fingerprint.
param([string]$Out = "freeze-valid.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")   # .NET file calls do not follow Set-Location
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-valid"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item -LiteralPath $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-VALID-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

$cpp = [IO.File]::ReadAllText("src/thermal/ThermalValidation.cpp")
$hdr = [IO.File]::ReadAllText("include/bettercad/thermal/ThermalValidation.hpp")
function BodyOf([string]$text, [string]$signature) {
    $a = $text.IndexOf($signature)
    if ($a -lt 0) { throw "signature not found: $signature" }
    $b = $text.IndexOf("`n}`n", $a)
    return $text.Substring($a, $b - $a + 2)
}

# The validation contract: the public header a later milestone relies on, with the headers whose types it returns or reads.
$c1 = (& $git hash-object include/bettercad/thermal/ThermalValidation.hpp).Trim()
$c2 = (& $git hash-object include/bettercad/thermal/ThermalResult.hpp).Trim()
$c3 = (& $git hash-object include/bettercad/thermal/ThermalAnalysisObject.hpp).Trim()
[IO.File]::WriteAllText("$s\contract-valid.txt", "ThermalValidation.hpp     $c1`nThermalResult.hpp         $c2`nThermalAnalysisObject.hpp $c3`n")
"ThermalValidation.hpp     $c1" | Out-File $o -Append
"ThermalResult.hpp         $c2" | Out-File $o -Append
"ThermalAnalysisObject.hpp $c3" | Out-File $o -Append
"P18_VALIDATION_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-valid.txt").Trim())" | Out-File $o -Append

# The policy: the rule order, every code's rule and severity, the mesh thresholds.
$rules = [regex]::Match($hdr, 'inline constexpr std::array<ValidationRule, kValidationRuleCount> kValidationRules\{[^}]*\};').Value
$sev = BodyOf $cpp 'ValidationSeverity severityOf(ValidationCode code) noexcept {'
$rule = BodyOf $cpp 'ValidationRule ruleOf(ValidationCode code) noexcept {'
$thr = BodyOf $cpp 'meshing::QualityThresholds thermalQualityThresholds() {'
$consts = [regex]::Matches($hdr, 'inline constexpr double kThermal[A-Za-z]+ = [^;]+;') | ForEach-Object { $_.Value }
[IO.File]::WriteAllText("$s\policy-valid.txt", $rules + "`n" + $sev + $rule + $thr + (($consts) -join "`n") + "`n")
"policy: rule order, severityOf, ruleOf, thermalQualityThresholds, constants" | Out-File $o -Append
foreach ($c in $consts) { "policy constant   $c" | Out-File $o -Append }
"P18_VALIDATION_POLICY_FINGERPRINT $((& $git hash-object "$s\policy-valid.txt").Trim())" | Out-File $o -Append

# The thermal mesh acceptance: the constants, the thresholds and the measurements that derive them.
[IO.File]::WriteAllText("$s\mesh-valid.txt", (($consts) -join "`n") + "`n" + $thr + "ThermalValidationLawTests.cpp " + (& $git hash-object tests/thermal/ThermalValidationLawTests.cpp).Trim() + "`nThermalValidationSweepTests.cpp " + (& $git hash-object tests/reference/ThermalValidationSweepTests.cpp).Trim() + "`n")
"P18_THERMAL_MESH_ACCEPTANCE_FINGERPRINT $((& $git hash-object "$s\mesh-valid.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckValidationOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'validation-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-valid.txt", (($own) -join "`n") + "`n")
foreach ($l in $own) { "ownership   $l" | Out-File $o -Append }
"VALIDATION_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-valid.txt").Trim())" | Out-File $o -Append
$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/linalg/CMakeLists.txt).Trim()
$d3 = (& $git hash-object src/structural/CMakeLists.txt).Trim()
$d4 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d5 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-valid.txt", "src/thermal/CMakeLists.txt     $d1`nsrc/linalg/CMakeLists.txt      $d2`nsrc/structural/CMakeLists.txt  $d3`nCheckLayering.cmake           $d4`nCheckTargetDependencies.cmake $d5`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"src/linalg/CMakeLists.txt      $d2" | Out-File $o -Append
"src/structural/CMakeLists.txt  $d3" | Out-File $o -Append
"CheckLayering.cmake           $d4" | Out-File $o -Append
"CheckTargetDependencies.cmake $d5" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-valid.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD): every thermal source and header except the
# new pair and the CMake list, every meshing file (P16 is not changed), and the linalg and structural solve files.
$untouched = @()
foreach ($d in 'src/thermal', 'include/bettercad/thermal') {
    foreach ($f in (Get-ChildItem -File $d | Where-Object { $_.Name -notmatch 'ThermalValidation|^CMakeLists' } | Sort-Object Name)) { $untouched += ($d + '/' + $f.Name) }
}
foreach ($d in 'src/meshing', 'include/bettercad/meshing') {
    foreach ($f in (Get-ChildItem -File $d | Sort-Object Name)) { $untouched += ($d + '/' + $f.Name) }
}
$untouched += 'src/linalg/SymmetricSolve.cpp', 'include/bettercad/linalg/SymmetricSolve.hpp', 'src/structural/StructuralSolve.cpp'
$equal = 0
$different = 0
foreach ($b in $untouched) {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse "HEAD:$b").Trim()
    if ($work -eq $head) { $equal++ } else { $different++ }
    "untouched {0,-62} work {1} head {2} {3}" -f $b, $work, $head, $(if ($work -eq $head) { "EQUAL" } else { "DIFFERENT" }) | Out-File $o -Append
}
"untouched files: $($untouched.Count) equal $equal different $different" | Out-File $o -Append
foreach ($b in 'src/thermal/ThermalValidation.cpp', 'tests/thermal/ThermalValidationTests.cpp', 'tests/thermal/ThermalValidationMappedTests.cpp', 'tests/thermal/ThermalValidationLawTests.cpp', 'tests/thermal/ThermalValidationTestSupport.hpp', 'tests/reference/ThermalValidationSweepTests.cpp', 'tests/compile_fail/ThermalValidationMisuse.cpp') {
    "blob {0,-58} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
