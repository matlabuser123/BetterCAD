# P18-REFMOD-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, and the milestone's own fingerprints:
# the contract (the reference-model builders), the reference suite (every test file and its registration), the analytical oracle, and the tolerances.
# ASCII ONLY: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
param([string]$Out = "freeze-ref.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-ref"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-REFMOD-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

function Hashes([string[]]$names) {
    $lines = @()
    foreach ($n in $names) { $lines += ("{0,-66} {1}" -f $n, (& $git hash-object $n).Trim()) }
    return $lines
}

# THE CONTRACT: what the reference documents declare (builders, catalog, runner loop and its build list).
$contract = Hashes @("examples/reference_models/ThermalReferenceModels.hpp", "examples/reference_models/ThermalModels.cpp",
                     "examples/reference_models/ThermalCatalog.cpp", "examples/reference_models/main.cpp", "examples/reference_models/CMakeLists.txt")
[IO.File]::WriteAllText("$s\contract-ref.txt", (($contract) -join "`n") + "`n")
foreach ($l in $contract) { $l | Out-File $o -Append }
"P18_REFMOD_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-ref.txt").Trim())" | Out-File $o -Append

# THE REFERENCE SUITE: every test source, the shared fixtures, the workflow, the registration and the zero-match groups.
$suiteFiles = @("tests/reference/ThermalReferenceSupport.hpp", "tests/reference/ThermalPatchSupport.hpp",
                "tests/reference/ThermalElementReferenceTests.cpp", "tests/reference/ThermalSteadyReferenceTests.cpp",
                "tests/reference/ThermalTransformRefineReferenceTests.cpp", "tests/reference/ThermalTransientReferenceTests.cpp",
                "tests/reference/ThermalFailureReferenceTests.cpp", "tests/reference/ThermalPersistenceReferenceTests.cpp",
                "tests/reference/ThermalIndependenceReferenceTests.cpp", "tests/reference/ThermalCliReferenceTests.cpp",
                "tests/cli/ThermalReferenceWorkflow.cmake", "tests/cli/ZeroMatchGuard.cmake", "tests/CMakeLists.txt")
$suite = Hashes $suiteFiles
[IO.File]::WriteAllText("$s\suite-ref.txt", (($suite) -join "`n") + "`n")
foreach ($l in $suite) { $l | Out-File $o -Append }
$cases = 0
foreach ($f in $suiteFiles | Where-Object { $_ -like "tests/reference/*Tests.cpp" }) {
    $cases += ([regex]::Matches([IO.File]::ReadAllText($f), '(?m)^TEST_CASE\("')).Count
}
"TEST_CASEs in the 8 reference test sources: $cases (= 47 in-process cases, which include the 2 independence cases, + 7 hidden CLI cases)" | Out-File $o -Append
"P18_REFERENCE_SUITE_FINGERPRINT $((& $git hash-object "$s\suite-ref.txt").Trim())" | Out-File $o -Append

# THE ANALYTICAL ORACLE: the header, byte for byte, and the gate that keeps it independent.
$oracle = Hashes @("tests/reference/ThermalAnalytic.hpp", "tests/reference/ThermalIndependenceReferenceTests.cpp")
[IO.File]::WriteAllText("$s\oracle-ref.txt", (($oracle) -join "`n") + "`n")
foreach ($l in $oracle) { $l | Out-File $o -Append }
"P18_ANALYTICAL_ORACLE_FINGERPRINT $((& $git hash-object "$s\oracle-ref.txt").Trim())" | Out-File $o -Append

# THE TOLERANCES: every Tolerance{...} literal and every kClass constant in the reference sources, in file order.
$tol = @()
foreach ($f in $suiteFiles | Where-Object { $_ -like "tests/reference/*" }) {
    $text = [IO.File]::ReadAllText($f)
    foreach ($m in [regex]::Matches($text, 'Tolerance\{[^}]*\}')) { $tol += "$f $($m.Value)" }
    foreach ($m in [regex]::Matches($text, 'kClass[AB]\{[^}]*\}')) { $tol += "$f $($m.Value)" }
    foreach ($m in [regex]::Matches($text, 'CHECK\([^;\r\n]*(<=|>=)\s*[0-9][^;\r\n]*\);')) { $tol += "$f $($m.Value -replace '\s+', ' ')" }
}
[IO.File]::WriteAllText("$s\tolerance-ref.txt", (($tol) -join "`n") + "`n")
"tolerance literals and numeric CHECK bounds: $($tol.Count)" | Out-File $o -Append
"P18_REFMOD_TOLERANCE_FINGERPRINT $((& $git hash-object "$s\tolerance-ref.txt").Trim())" | Out-File $o -Append

# NOTHING QUALIFIED MAY HAVE CHANGED: src, include and apps are byte-identical to HEAD (no file is expected to differ).
$untouched = @()
foreach ($d in 'src', 'include', 'apps') {
    foreach ($f in (Get-ChildItem -File -Recurse $d | Sort-Object FullName)) { $untouched += $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/') }
}
$equal = 0; $different = @(); $new = @()
foreach ($b in $untouched) {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse --verify --quiet "HEAD:$b" 2>$null)
    if ($head) { $head = $head.Trim() } else { $head = $null }
    if ($null -eq $head) { $new += $b } elseif ($work -eq $head) { $equal++ } else { $different += $b }
}
"src + include + apps files: $($untouched.Count) equal $equal different $($different.Count) new $($new.Count)" | Out-File $o -Append
foreach ($b in $different) { "  DIFFERENT $b (UNEXPECTED)" | Out-File $o -Append }
foreach ($b in $new) { "  NEW       $b (UNEXPECTED)" | Out-File $o -Append }
"unexpected changes under src, include, apps: $($different.Count + $new.Count)" | Out-File $o -Append
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
