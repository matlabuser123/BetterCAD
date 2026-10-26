# P18-QUAL-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, and the EIGHTEEN contract fingerprints of the
# brief, each the hash of a list of "path git-hash" lines in a fixed order, so a change to any one file moves exactly the fingerprints that contain it.
# Run it twice before the qualification (the two outputs must be identical) and once after (the post-run output must equal the pre-run one).
# ASCII ONLY: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
param([string]$Out = "freeze-qual.txt", [string]$Scratch = "")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
if ($Scratch -eq "") { $Scratch = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad" }
Set-Location $root
[IO.Directory]::SetCurrentDirectory($root)
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$Scratch\scratch-index-qual"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$Scratch\$Out"
"P18-QUAL-001 freeze" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) { "{0,-18} {1}" -f $p, (& $git rev-parse "${tree}:$p").Trim() | Out-File $o -Append }

function Files([string[]]$patterns) {
    $list = @()
    foreach ($p in $patterns) {
        if ($p -match '[\*\?]') {
            $dir = Split-Path $p -Parent; $leaf = Split-Path $p -Leaf
            foreach ($f in (Get-ChildItem -File $dir -Filter $leaf | Sort-Object Name)) { $list += (Join-Path $dir $f.Name).Replace('\', '/') }
        } else { $list += $p.Replace('\', '/') }
    }
    return ($list | Select-Object -Unique)
}
function Fingerprint([string]$name, [string[]]$patterns) {
    $lines = @()
    foreach ($f in (Files $patterns)) {
        if (-not (Test-Path $f)) { $lines += ("{0,-70} MISSING" -f $f); continue }
        $lines += ("{0,-70} {1}" -f $f, (& $git hash-object $f).Trim())
    }
    $tmp = "$Scratch\fp-$name.txt"
    [IO.File]::WriteAllText($tmp, ($lines -join "`n") + "`n")
    $h = (& $git hash-object $tmp).Trim()
    "" | Out-File $o -Append
    "== $name  ($($lines.Count) files)" | Out-File $o -Append
    foreach ($l in $lines) { "   $l" | Out-File $o -Append }
    "P18_QUAL_${name}_FINGERPRINT $h" | Out-File $o -Append
}

Fingerprint "ARCHITECTURE" @("docs/architecture/decisions/ADR-049*.md", "docs/architecture/decisions/ADR-050*.md", "docs/architecture/decisions/ADR-051*.md", "docs/architecture/decisions/ADR-052*.md",
    "docs/architecture/decisions/ADR-053*.md", "docs/architecture/decisions/ADR-054*.md", "docs/architecture/decisions/ADR-055*.md", "docs/architecture/decisions/ADR-056*.md", "docs/architecture/decisions/ADR-057*.md",
    "tests/architecture/CheckLayering.cmake", "tests/architecture/CheckTargetDependencies.cmake", "tests/architecture/CMakeLists.txt", "src/thermal/CMakeLists.txt", "src/linalg/CMakeLists.txt")
Fingerprint "AUTHORITY" @("include/bettercad/thermal/ThermalData.hpp", "src/thermal/ThermalData.cpp", "include/bettercad/thermal/ThermalAnalysisObject.hpp", "src/thermal/ThermalAnalysisObject.cpp",
    "include/bettercad/thermal/ThermalAnalysis.hpp", "src/thermal/ThermalAnalysis.cpp", "include/bettercad/thermal/ThermalMaterial.hpp", "src/thermal/ThermalMaterial.cpp",
    "include/bettercad/thermal/ThermalTarget.hpp", "src/thermal/ThermalTarget.cpp", "tests/architecture/CheckValidationOwnership.cmake")
Fingerprint "CURRENTNESS" @("include/bettercad/thermal/ThermalDof.hpp", "include/bettercad/thermal/ThermalLoadVector.hpp", "include/bettercad/thermal/ThermalBoundary.hpp", "include/bettercad/thermal/ThermalSystem.hpp",
    "include/bettercad/thermal/ThermalSolve.hpp", "include/bettercad/thermal/ThermalTransient.hpp", "include/bettercad/thermal/ThermalPost.hpp", "include/bettercad/thermal/ThermalBalance.hpp",
    "include/bettercad/thermal/ThermalResult.hpp", "src/thermal/ThermalResult.cpp", "include/bettercad/renderer/ThermalInspection.hpp", "src/renderer/ThermalInspection.cpp",
    "tests/qualification/P18ThermalQualificationTests.cpp")
Fingerprint "DOF" @("include/bettercad/thermal/ThermalDof.hpp", "src/thermal/ThermalDof.cpp")
Fingerprint "ELEMENT" @("include/bettercad/thermal/ThermalElement.hpp", "src/thermal/ThermalElement.cpp", "include/bettercad/thermal/ThermalConvection.hpp", "src/thermal/ThermalConvection.cpp")
Fingerprint "LOADS_BCS" @("include/bettercad/thermal/ThermalLoadVector.hpp", "src/thermal/ThermalLoadVector.cpp", "include/bettercad/thermal/ThermalBoundary.hpp", "src/thermal/ThermalBoundary.cpp",
    "include/bettercad/thermal/ThermalTarget.hpp", "src/thermal/ThermalTarget.cpp")
Fingerprint "ASSEMBLY" @("include/bettercad/thermal/ThermalSystem.hpp", "src/thermal/ThermalSystem.cpp", "include/bettercad/thermal/ThermalSparse.hpp", "src/thermal/ThermalSparse.cpp")
Fingerprint "SOLVER" @("include/bettercad/thermal/ThermalSolve.hpp", "src/thermal/ThermalSolve.cpp", "include/bettercad/linalg/SymmetricSolve.hpp", "src/linalg/SymmetricSolve.cpp")
Fingerprint "TRANSIENT" @("include/bettercad/thermal/ThermalTransient.hpp", "src/thermal/ThermalTransient.cpp")
Fingerprint "POST" @("include/bettercad/thermal/ThermalPost.hpp", "src/thermal/ThermalPost.cpp")
Fingerprint "BALANCE" @("include/bettercad/thermal/ThermalBalance.hpp", "src/thermal/ThermalBalance.cpp")
Fingerprint "VALIDATION" @("include/bettercad/thermal/ThermalValidation.hpp", "src/thermal/ThermalValidation.cpp", "include/bettercad/thermal/ThermalAnalysisRun.hpp", "src/thermal/ThermalAnalysisRun.cpp")
Fingerprint "VIZ" @("include/bettercad/renderer/Thermal*.hpp", "src/renderer/Thermal*.cpp", "src/renderer/occt/OcctOverlayPresentation.cpp", "src/renderer/occt/OcctViewer.cpp", "include/bettercad/renderer/Viewer.hpp")
Fingerprint "COMMANDS" @("include/bettercad/thermal/ThermalCommands.hpp", "src/thermal/ThermalCommands.cpp")
Fingerprint "PERSISTENCE" @("src/io/json/ThermalAnalysisJson.cpp", "src/io/json/DocumentJson.cpp", "src/io/json/DatumJson.cpp")
Fingerprint "CLI" @("apps/bettercad_cli/Thermal*.cpp", "apps/bettercad_cli/Thermal*.hpp")
Fingerprint "REFERENCE_SUITE" @("examples/reference_models/ThermalReferenceModels.hpp", "examples/reference_models/ThermalModels.cpp", "examples/reference_models/ThermalCatalog.cpp",
    "tests/reference/Thermal*.cpp", "tests/reference/Thermal*.hpp", "tests/cli/ThermalReferenceWorkflow.cmake", "tests/cli/ThermalWorkflow.cmake", "tests/cli/ZeroMatchGuard.cmake")

# THE TOLERANCE CONTRACT: every named numeric constant of the thermal and linalg modules and the two settings defaults, extracted by pattern, in file order.
$tol = @()
$tolFiles = @(Get-ChildItem -File -Recurse src/thermal, include/bettercad/thermal, src/linalg, include/bettercad/linalg | Sort-Object FullName)
foreach ($f in $tolFiles) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
    foreach ($line in (Get-Content $f.FullName)) {
        if ($line -match '^\s*(inline\s+)?constexpr\s+(double|int|std::size_t|std::uint\d+_t)\s+k\w+\s*(=|\{)' -or
            $line -match '^\s*double\s+(relativeResidualTolerance|pivotFloor)\s*=') { $tol += "$rel $($line.Trim())" }
    }
}
[IO.File]::WriteAllText("$Scratch\fp-TOLERANCE.txt", ($tol -join "`n") + "`n")
"" | Out-File $o -Append
"== TOLERANCE  ($($tol.Count) named constants and defaults)" | Out-File $o -Append
foreach ($l in $tol) { "   $l" | Out-File $o -Append }
"P18_QUAL_TOLERANCE_FINGERPRINT $((& $git hash-object "$Scratch\fp-TOLERANCE.txt").Trim())" | Out-File $o -Append

# TEST INVENTORY at the freeze: TEST_CASE counts per area, so the zero-match tables can be compared with an independent count.
"" | Out-File $o -Append
"== TEST INVENTORY (TEST_CASE( occurrences by directory)" | Out-File $o -Append
foreach ($d in "tests/thermal", "tests/reference", "tests/cli", "tests/renderer", "tests/io", "tests/qualification", "tests/linalg", "tests/compile_fail") {
    $n = 0
    foreach ($f in (Get-ChildItem -File -Recurse $d -Include *.cpp)) { $n += ([regex]::Matches([IO.File]::ReadAllText($f.FullName), '(?m)^TEST_CASE\(')).Count }
    "   {0,-22} {1}" -f $d, $n | Out-File $o -Append
}

# WHAT DIFFERS FROM HEAD under src, include, apps, tests, examples (the milestone's own changes, all expected and all listed).
"" | Out-File $o -Append
"== CHANGES against HEAD in the eight qualified paths (git status --short)" | Out-File $o -Append
& $git status --short -- $paths | Out-File $o -Append
"== other uncommitted paths" | Out-File $o -Append
& $git status --short | Where-Object { $_ -notmatch ' (apps|include|src|tests|examples|cmake)/' } | Out-File $o -Append
Get-Content $o
