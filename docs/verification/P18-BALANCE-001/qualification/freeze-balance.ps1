# P18-BALANCE-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it,
# the recovery-contract fingerprint, the field-descriptor fingerprint and the ownership fingerprint.
param([string]$Out = "freeze-balance.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-balance"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$o = "$s\$Out"
"P18-BALANCE-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

# The balance contract: the public header a later milestone relies on, with the data header whose HeatBalance it fills.
$c1 = (& $git hash-object include/bettercad/thermal/ThermalBalance.hpp).Trim()
$c2 = (& $git hash-object include/bettercad/thermal/ThermalData.hpp).Trim()
[IO.File]::WriteAllText("$s\contract-balance.txt", "ThermalBalance.hpp   $c1`nThermalData.hpp      $c2`n")
"ThermalBalance.hpp   $c1" | Out-File $o -Append
"ThermalData.hpp      $c2" | Out-File $o -Append
"P18_BALANCE_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-balance.txt").Trim())" | Out-File $o -Append

$cpp = Get-Content src/thermal/ThermalBalance.cpp -Raw
$hdr = Get-Content include/bettercad/thermal/ThermalBalance.hpp -Raw
$data = Get-Content include/bettercad/thermal/ThermalData.hpp -Raw

# The sign convention: every line that fixes a sign, plus P18-DATA-001's netImbalance.
$signLines = ($cpp -split "`n") | Where-Object { $_ -match 'fullResidual\[dof\]|Power::fromSi\(-out\)|\.heatInto = Power::fromSi\(out\)|r\[i\] = \(\*kt\)\[i\] - q\[i\]|r\[i\] = \(\*cdt\)\[i\] / timeStep \+ \(\*kt\)\[i\] - q\[i\]|stored->si\(\) / timeStep|const Power residual = out.residual\(\)|categoriesOf\(input, next, step\)|grid.stepAt\(k - 1\)' }
$netStart = $data.IndexOf('[[nodiscard]] Power netImbalance() const noexcept {')
$netEnd = $data.IndexOf('}', $data.IndexOf('storedEnergyRate.value_or(Power{});', $netStart))
$net = $data.Substring($netStart, $netEnd - $netStart + 1)
[IO.File]::WriteAllText("$s\sign-balance.txt", (($signLines | ForEach-Object { $_.Trim() }) -join "`n") + "`n" + $net + "`n")
foreach ($l in $signLines) { "sign line   $($l.Trim())" | Out-File $o -Append }
"sign line   netImbalance: $(($net -replace '\s+', ' '))" | Out-File $o -Append
"P18_BALANCE_SIGN_CONVENTION_FINGERPRINT $((& $git hash-object "$s\sign-balance.txt").Trim())" | Out-File $o -Append

# The tolerance: the three constants and the function that applies them.
$consts = [regex]::Matches($hdr, 'inline constexpr double kBalance[A-Za-z]+ = [^;]+;') | ForEach-Object { $_.Value }
$ts = $cpp.IndexOf('bool balanceWithinTolerance(Power residual, Power gross, Power arithmetic) noexcept {')
$te = $cpp.IndexOf("`n}`n", $ts)
$fn = $cpp.Substring($ts, $te - $ts + 2)
[IO.File]::WriteAllText("$s\tolerance-balance.txt", (($consts -join "`n") + "`n" + $fn + "`n"))
foreach ($c in $consts) { "tolerance constant   $c" | Out-File $o -Append }
"P18_BALANCE_TOLERANCE_FINGERPRINT $((& $git hash-object "$s\tolerance-balance.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckBalanceOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'balance-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-balance.txt", (($own) -join "`n") + "`n")
foreach ($l in $own) { "ownership   $l" | Out-File $o -Append }
"BALANCE_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-balance.txt").Trim())" | Out-File $o -Append
$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/linalg/CMakeLists.txt).Trim()
$d3 = (& $git hash-object src/structural/CMakeLists.txt).Trim()
$d4 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d5 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-balance.txt", "src/thermal/CMakeLists.txt     $d1`nsrc/linalg/CMakeLists.txt      $d2`nsrc/structural/CMakeLists.txt  $d3`nCheckLayering.cmake           $d4`nCheckTargetDependencies.cmake $d5`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"src/linalg/CMakeLists.txt      $d2" | Out-File $o -Append
"src/structural/CMakeLists.txt  $d3" | Out-File $o -Append
"CheckLayering.cmake           $d4" | Out-File $o -Append
"CheckTargetDependencies.cmake $d5" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-balance.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD).
$untouched = 'src/thermal/ThermalSolve.cpp','include/bettercad/thermal/ThermalSolve.hpp','src/thermal/ThermalTransient.cpp','include/bettercad/thermal/ThermalTransient.hpp','src/thermal/ThermalPost.cpp','include/bettercad/thermal/ThermalPost.hpp','src/thermal/ThermalElement.cpp','include/bettercad/thermal/ThermalElement.hpp','include/bettercad/thermal/ThermalData.hpp','src/thermal/ThermalData.cpp','src/thermal/ThermalDof.cpp','include/bettercad/thermal/ThermalDof.hpp','src/thermal/ThermalLoadVector.cpp','include/bettercad/thermal/ThermalLoadVector.hpp','src/thermal/ThermalBoundary.cpp','include/bettercad/thermal/ThermalBoundary.hpp','src/thermal/ThermalConvection.cpp','include/bettercad/thermal/ThermalConvection.hpp','src/thermal/ThermalSystem.cpp','include/bettercad/thermal/ThermalSystem.hpp','src/linalg/SymmetricSolve.cpp','include/bettercad/linalg/SymmetricSolve.hpp','src/structural/StructuralSolve.cpp'
foreach ($b in $untouched) {
    if (-not (Test-Path $b)) { continue }
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse "HEAD:$b").Trim()
    "untouched {0,-52} work {1} head {2} {3}" -f $b, $work, $head, $(if ($work -eq $head) { "EQUAL" } else { "DIFFERENT" }) | Out-File $o -Append
}
foreach ($b in 'src/thermal/ThermalBalance.cpp','tests/thermal/ThermalBalanceTests.cpp','tests/thermal/ThermalBalanceMappedTests.cpp','tests/thermal/ThermalBalanceTestSupport.hpp','tests/compile_fail/ThermalBalanceMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
