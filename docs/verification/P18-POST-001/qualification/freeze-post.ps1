# P18-POST-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it,
# the recovery-contract fingerprint, the field-descriptor fingerprint and the ownership fingerprint.
param([string]$Out = "freeze-post.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-post"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$o = "$s\$Out"
"P18-POST-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

# The recovery contract: the public header a later milestone relies on, with the headers whose types it returns.
$c1 = (& $git hash-object include/bettercad/thermal/ThermalPost.hpp).Trim()
$c2 = (& $git hash-object include/bettercad/thermal/ThermalElement.hpp).Trim()
$c3 = (& $git hash-object include/bettercad/thermal/ThermalData.hpp).Trim()
[IO.File]::WriteAllText("$s\contract-post.txt", "ThermalPost.hpp      $c1`nThermalElement.hpp   $c2`nThermalData.hpp      $c3`n")
"ThermalPost.hpp      $c1" | Out-File $o -Append
"ThermalElement.hpp   $c2" | Out-File $o -Append
"ThermalData.hpp      $c3" | Out-File $o -Append
"P18_POST_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-post.txt").Trim())" | Out-File $o -Append

# The field descriptors: the four descriptorOf cases, which fix location, components, dimension and unit.
$cpp = Get-Content src/thermal/ThermalPost.cpp -Raw
$begin = $cpp.IndexOf('ThermalFieldDescriptor descriptorOf(ThermalPostField field) noexcept {')
$end = $cpp.IndexOf('std::string_view toString(PostProblem problem) noexcept {')
$descriptors = $cpp.Substring($begin, $end - $begin)
[IO.File]::WriteAllText("$s\descriptors-post.txt", $descriptors)
"FIELD_DESCRIPTOR_FINGERPRINT $((& $git hash-object "$s\descriptors-post.txt").Trim())" | Out-File $o -Append

# The Fourier line: the one place q = -k grad(T) is written.
$lines = ($cpp -split "`n") | Where-Object { $_ -match 'HeatFlux\{\} - conductivity \* gradient\.' }
[IO.File]::WriteAllText("$s\fourier-post.txt", (($lines | ForEach-Object { $_.Trim() }) -join "`n") + "`n")
foreach ($l in $lines) { "fourier line   $($l.Trim())" | Out-File $o -Append }
"FOURIER_LINE_FINGERPRINT $((& $git hash-object "$s\fourier-post.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its three fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckRecoveryOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'recovery-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-post.txt", (($own) -join "`n") + "`n")
foreach ($l in $own) { "ownership   $l" | Out-File $o -Append }
"RECOVERY_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-post.txt").Trim())" | Out-File $o -Append

$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/linalg/CMakeLists.txt).Trim()
$d3 = (& $git hash-object src/structural/CMakeLists.txt).Trim()
$d4 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d5 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-post.txt", "src/thermal/CMakeLists.txt     $d1`nsrc/linalg/CMakeLists.txt      $d2`nsrc/structural/CMakeLists.txt  $d3`nCheckLayering.cmake           $d4`nCheckTargetDependencies.cmake $d5`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"src/linalg/CMakeLists.txt      $d2" | Out-File $o -Append
"src/structural/CMakeLists.txt  $d3" | Out-File $o -Append
"CheckLayering.cmake           $d4" | Out-File $o -Append
"CheckTargetDependencies.cmake $d5" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-post.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD).
$untouched = 'src/thermal/ThermalSolve.cpp','include/bettercad/thermal/ThermalSolve.hpp','src/thermal/ThermalTransient.cpp','include/bettercad/thermal/ThermalTransient.hpp','src/thermal/ThermalElement.cpp','include/bettercad/thermal/ThermalElement.hpp','include/bettercad/thermal/ThermalData.hpp','src/thermal/ThermalData.cpp','src/thermal/ThermalDof.cpp','include/bettercad/thermal/ThermalDof.hpp','src/linalg/SymmetricSolve.cpp','include/bettercad/linalg/SymmetricSolve.hpp','src/structural/StructuralSolve.cpp'
foreach ($b in $untouched) {
    if (-not (Test-Path $b)) { continue }
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse "HEAD:$b").Trim()
    "untouched {0,-52} work {1} head {2} {3}" -f $b, $work, $head, $(if ($work -eq $head) { "EQUAL" } else { "DIFFERENT" }) | Out-File $o -Append
}
foreach ($b in 'src/thermal/ThermalPost.cpp','tests/thermal/ThermalPostTests.cpp','tests/thermal/ThermalPostMappedTests.cpp','tests/thermal/ThermalPostTestSupport.hpp','tests/compile_fail/ThermalPostMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
