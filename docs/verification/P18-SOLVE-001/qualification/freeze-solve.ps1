# P18-SOLVE-001 freeze: the eight-path fingerprint from a scratch index, exactly as
# qualify.cmd computes it, the steady-solver contract fingerprint and the ADR-050 dependency
# fingerprint.
param([string]$Out = "freeze-solve.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-solve"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$o = "$s\$Out"
"P18-SOLVE-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}
$c1 = (& $git hash-object include/bettercad/thermal/ThermalSolve.hpp).Trim()
$c2 = (& $git hash-object include/bettercad/linalg/SymmetricSolve.hpp).Trim()
[IO.File]::WriteAllText("$s\contract-solve.txt", "ThermalSolve.hpp      $c1`nSymmetricSolve.hpp    $c2`n")
"ThermalSolve.hpp      $c1" | Out-File $o -Append
"SymmetricSolve.hpp    $c2" | Out-File $o -Append
"P18_STEADY_SOLVER_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-solve.txt").Trim())" | Out-File $o -Append
$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/linalg/CMakeLists.txt).Trim()
$d3 = (& $git hash-object src/structural/CMakeLists.txt).Trim()
$d4 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d5 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-solve.txt", "src/thermal/CMakeLists.txt     $d1`nsrc/linalg/CMakeLists.txt      $d2`nsrc/structural/CMakeLists.txt  $d3`nCheckLayering.cmake           $d4`nCheckTargetDependencies.cmake $d5`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"src/linalg/CMakeLists.txt      $d2" | Out-File $o -Append
"src/structural/CMakeLists.txt  $d3" | Out-File $o -Append
"CheckLayering.cmake           $d4" | Out-File $o -Append
"CheckTargetDependencies.cmake $d5" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-solve.txt").Trim())" | Out-File $o -Append
foreach ($b in 'src/thermal/ThermalSolve.cpp','src/linalg/SymmetricSolve.cpp','src/structural/StructuralSolve.cpp','tests/thermal/ThermalSolveTests.cpp','tests/thermal/ThermalSolveMappedTests.cpp','tests/thermal/ThermalSolveTestSupport.hpp','tests/linalg/SymmetricSolveTests.cpp','tests/compile_fail/ThermalSolveMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o