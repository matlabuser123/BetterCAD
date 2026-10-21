# P18-TRANSIENT-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd
# computes it, the transient-solver contract fingerprint and the time-grid policy fingerprint.
param([string]$Out = "freeze-transient.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-transient"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$o = "$s\$Out"
"P18-TRANSIENT-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

# The transient solver contract: the one public header a later milestone relies on, and the steady one it is built on.
$c1 = (& $git hash-object include/bettercad/thermal/ThermalTransient.hpp).Trim()
$c2 = (& $git hash-object include/bettercad/thermal/ThermalSolve.hpp).Trim()
[IO.File]::WriteAllText("$s\contract-transient.txt", "ThermalTransient.hpp   $c1`nThermalSolve.hpp       $c2`n")
"ThermalTransient.hpp   $c1" | Out-File $o -Append
"ThermalSolve.hpp       $c2" | Out-File $o -Append
"P18_TRANSIENT_SOLVER_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-transient.txt").Trim())" | Out-File $o -Append

# The time-grid policy: the three constants and the function that applies them.
$header = Get-Content include/bettercad/thermal/ThermalTransient.hpp -Raw
$cpp = Get-Content src/thermal/ThermalTransient.cpp -Raw
$constants = [regex]::Matches($header, 'inline constexpr [^\r\n]*(kTimeGridTolerance|kMaxTimeSteps|kMaxStoredValues)[^\r\n]*;') | ForEach-Object { $_.Value }
$begin = $cpp.IndexOf('std::expected<TimeGrid, TransientFailure> TimeGrid::create')
$end = $cpp.IndexOf('std::vector<double> TimeGrid::times() const')
$policy = $cpp.Substring($begin, $end - $begin)
[IO.File]::WriteAllText("$s\policy-transient.txt", (($constants -join "`n") + "`n" + $policy))
foreach ($c in $constants) { "policy constant   $c" | Out-File $o -Append }
"TIME_GRID_POLICY_FINGERPRINT $((& $git hash-object "$s\policy-transient.txt").Trim())" | Out-File $o -Append

$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/linalg/CMakeLists.txt).Trim()
$d3 = (& $git hash-object src/structural/CMakeLists.txt).Trim()
$d4 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d5 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-transient.txt", "src/thermal/CMakeLists.txt     $d1`nsrc/linalg/CMakeLists.txt      $d2`nsrc/structural/CMakeLists.txt  $d3`nCheckLayering.cmake           $d4`nCheckTargetDependencies.cmake $d5`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"src/linalg/CMakeLists.txt      $d2" | Out-File $o -Append
"src/structural/CMakeLists.txt  $d3" | Out-File $o -Append
"CheckLayering.cmake           $d4" | Out-File $o -Append
"CheckTargetDependencies.cmake $d5" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-transient.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD).
foreach ($b in 'src/thermal/ThermalSolve.cpp','include/bettercad/thermal/ThermalSolve.hpp','src/linalg/SymmetricSolve.cpp','include/bettercad/linalg/SymmetricSolve.hpp','src/structural/StructuralSolve.cpp') {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse "HEAD:$b").Trim()
    "untouched {0,-52} work {1} head {2} {3}" -f $b, $work, $head, $(if ($work -eq $head) { "EQUAL" } else { "DIFFERENT" }) | Out-File $o -Append
}
foreach ($b in 'src/thermal/ThermalTransient.cpp','tests/thermal/ThermalTransientTests.cpp','tests/thermal/ThermalTransientMappedTests.cpp','tests/thermal/ThermalTransientTestSupport.hpp','tests/compile_fail/ThermalTransientMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
