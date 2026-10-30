# P19-MESH-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, and the milestone's own fingerprints:
# the CFD mesh contract, the P16 code it reads (which must NOT change), its tests and ADR-063.
# Run it twice before the qualification (the two outputs must be identical) and once after (the post-run output must equal the pre-run one).
# ASCII ONLY: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
# THE SCRATCH DIRECTORY IS DURABLE, NOT A SESSION TEMP FOLDER. It defaulted to an
# agent session's scratchpad under %TEMP%\claude\...\<session guid>, which is deleted
# when that session ends. That breaks the freeze outright (Out-File into a missing
# directory) or, worse, lets the freeze succeed and leaves commit-mesh.ps1 unable to
# read the freeze file it must compare against -- discovered SEVEN HOURS LATER, after
# the qualification. Both scripts now default to the SAME durable directory and create
# it if absent. Keep -Scratch identical in both, or pass it to neither.
#
# $RootDir and $GitExe are parameters for the same reason: with all three machine
# paths overridable the freeze can be REHEARSED on another checkout before the long
# run. The defaults are the qualification machine's, so its behaviour is unchanged.
param(
    [string]$Out = "freeze-mesh.txt",
    [string]$Scratch = "",
    [string]$RootDir = "C:\Users\Hasib\Desktop\CAD",
    [string]$GitExe = ""
)
if ($Scratch -eq "") { $Scratch = "C:\bettercad\p19-mesh-scratch" }
if (-not (Test-Path -LiteralPath $Scratch)) { New-Item -ItemType Directory -Path $Scratch -Force | Out-Null }
if ($GitExe -eq "") {
    $GitExe = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
    if (-not (Test-Path -LiteralPath $GitExe)) { $GitExe = "git" }
}
$git = $GitExe
$root = $RootDir
Set-Location $root
[IO.Directory]::SetCurrentDirectory($root)
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$Scratch\scratch-index-mesh"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$Scratch\$Out"
"P19-MESH-001 freeze" | Out-File $o
"scratch   $Scratch" | Out-File $o -Append
"root      $root" | Out-File $o -Append
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
    $tmp = "$Scratch\fp-mesh-$name.txt"
    [IO.File]::WriteAllText($tmp, ($lines -join "`n") + "`n")
    "" | Out-File $o -Append
    "== $name  ($($lines.Count) files)" | Out-File $o -Append
    foreach ($l in $lines) { "   $l" | Out-File $o -Append }
    "P19_MESH_${name}_FINGERPRINT $((& $git hash-object $tmp).Trim())" | Out-File $o -Append
}
Fingerprint "CFD_MESH" @("include/bettercad/cfd/CfdMesh.hpp", "src/cfd/CfdMesh.cpp", "src/cfd/CMakeLists.txt", "include/bettercad/cfd/CfdAnalysis.hpp",
    "src/cfd/CfdAnalysis.cpp")
# The P16 code the gate reads. This milestone changes none of it; the fingerprint shows that, and HEAD-equality is checked below.
Fingerprint "P16_READ" @("include/bettercad/meshing/Mesh.hpp", "src/meshing/Mesh.cpp", "include/bettercad/meshing/VolumeMesh.hpp", "src/meshing/VolumeMesh.cpp",
    "include/bettercad/meshing/GeometryMeshMap.hpp", "src/meshing/GeometryMeshMap.cpp", "include/bettercad/meshing/GeometryPreparation.hpp",
    "include/bettercad/meshing/MeshIds.hpp")
Fingerprint "MESH_TESTS" @("tests/cfd/CfdMeshTests.cpp", "tests/reference/MeshTestSupport.hpp", "examples/reference_models/MeshReferenceModels.hpp",
    "tests/CMakeLists.txt", "tests/cli/ZeroMatchGuard.cmake")
Fingerprint "ADRS" @("docs/architecture/decisions/ADR-063*.md")
"" | Out-File $o -Append
"== P16 files changed against HEAD (must be empty)" | Out-File $o -Append
& $git status --short -- include/bettercad/meshing src/meshing | Out-File $o -Append
"" | Out-File $o -Append
"== CHANGES against HEAD in the eight qualified paths (git status --short)" | Out-File $o -Append
& $git status --short -- $paths | Out-File $o -Append
"== other uncommitted paths" | Out-File $o -Append
& $git status --short | Where-Object { $_ -notmatch ' (apps|include|src|tests|examples|cmake)/' } | Out-File $o -Append
Get-Content $o
