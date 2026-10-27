# P19-DATA-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, and the milestone's own fingerprints:
# the CFD data model (core's identities, the three headers and sources, the CMake), its tests (runtime, compile-fail, registrations) and ADR-061.
# Run it twice before the qualification (the two outputs must be identical) and once after (the post-run output must equal the pre-run one).
# ASCII ONLY: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
param([string]$Out = "freeze-data.txt", [string]$Scratch = "")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
if ($Scratch -eq "") { $Scratch = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad" }
Set-Location $root
[IO.Directory]::SetCurrentDirectory($root)
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$Scratch\scratch-index-data"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$Scratch\$Out"
"P19-DATA-001 freeze" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) { "{0,-18} {1}" -f $p, (& $git rev-parse "${tree}:$p").Trim() | Out-File $o -Append }

function Files([string[]]$patterns) {
    $list = @()
    foreach ($p in $patterns) {
        if ($p -match '[\*\?]') {
            foreach ($f in (Get-ChildItem -File -Path $p | Sort-Object FullName)) { $list += $f.FullName.Substring($root.Length + 1).Replace('\', '/') }
        } else { $list += $p.Replace('\', '/') }
    }
    return ($list | Select-Object -Unique)
}
function Fingerprint([string]$name, [string[]]$patterns) {
    $lines = @()
    foreach ($f in (Files $patterns)) {
        if (-not (Test-Path $f)) { $lines += ("{0,-90} MISSING" -f $f); continue }
        $lines += ("{0,-90} {1}" -f $f, (& $git hash-object $f).Trim())
    }
    $tmp = "$Scratch\fp-data-$name.txt"
    [IO.File]::WriteAllText($tmp, ($lines -join "`n") + "`n")
    "" | Out-File $o -Append
    "== $name  ($($lines.Count) files)" | Out-File $o -Append
    foreach ($l in $lines) { "   $l" | Out-File $o -Append }
    "P19_DATA_${name}_FINGERPRINT $((& $git hash-object $tmp).Trim())" | Out-File $o -Append
}
Fingerprint "CFD_DATA_MODEL" @("include/bettercad/core/Id.hpp", "include/bettercad/cfd/CfdData.hpp", "include/bettercad/cfd/CfdAnalysisObject.hpp",
    "include/bettercad/cfd/CfdResult.hpp", "include/bettercad/cfd/CfdAnalysis.hpp", "src/cfd/CfdData.cpp", "src/cfd/CfdAnalysisObject.cpp",
    "src/cfd/CfdResult.cpp", "src/cfd/CMakeLists.txt")
Fingerprint "CFD_DATA_TESTS" @("tests/cfd/CfdDataTests.cpp", "tests/compile_fail/CfdIdMisuse.cpp", "tests/compile_fail/CMakeLists.txt",
    "tests/CMakeLists.txt", "tests/cli/ZeroMatchGuard.cmake")
Fingerprint "ADRS" @("docs/architecture/decisions/ADR-061*.md")
"" | Out-File $o -Append
"== CHANGES against HEAD in the eight qualified paths (git status --short)" | Out-File $o -Append
& $git status --short -- $paths | Out-File $o -Append
"== other uncommitted paths" | Out-File $o -Append
& $git status --short | Where-Object { $_ -notmatch ' (apps|include|src|tests|examples|cmake)/' } | Out-File $o -Append
Get-Content $o
