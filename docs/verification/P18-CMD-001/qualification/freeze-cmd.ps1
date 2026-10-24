# P18-CMD-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, the command contract fingerprint, the
# canonical-history-schema fingerprint, the undo/redo-policy fingerprint, the source-invalidation-policy fingerprint, the ownership fingerprint and
# the dependency fingerprint.
param([string]$Out = "freeze-cmd.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")   # .NET file calls do not follow Set-Location
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-cmd"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item -LiteralPath $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-CMD-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

function BodyOf([string]$text, [string]$signature) {
    $a = $text.IndexOf($signature)
    if ($a -lt 0) { throw "signature not found: $signature" }
    $b = $text.IndexOf("`n}`n", $a)
    return $text.Substring($a, $b - $a + 2)
}
$hdr = [IO.File]::ReadAllText("include/bettercad/thermal/ThermalCommands.hpp")
$cpp = [IO.File]::ReadAllText("src/thermal/ThermalCommands.cpp")

# The command contract: the one public header a later milestone (persistence, the CLI, the GUI) relies on, and the three unchanged canonical headers
# whose types it carries.
$names = "ThermalCommands.hpp", "ThermalAnalysisObject.hpp", "ThermalData.hpp", "ThermalBoundary.hpp", "ThermalTarget.hpp"
$lines = @()
foreach ($n in $names) {
    $p = "include/bettercad/thermal/$n"
    if (Test-Path $p) { $lines += ("{0,-28} {1}" -f $n, (& $git hash-object $p).Trim()) } else { $lines += ("{0,-28} (absent)" -f $n) }
}
[IO.File]::WriteAllText("$s\contract-cmd.txt", (($lines) -join "`n") + "`n")
foreach ($l in $lines) { $l | Out-File $o -Append }
"P18_CMD_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-cmd.txt").Trim())" | Out-File $o -Append

# The canonical history schema: every data member of every command class, in declaration order. This is what a command CAN hold; the compile-fail
# group and the gate state what it cannot. A member added to a command changes this file.
$members = @()
foreach ($line in ($hdr -split "`n")) {
    if ($line -match '^class .*Command' ) { $members += ($line.Trim()) }
    elseif ($line -match '^    [A-Za-z_][^(]* \w+_(\{\})?;\s*$') { $members += ("    " + $line.Trim()) }
}
[IO.File]::WriteAllText("$s\schema-cmd.txt", (($members) -join "`n") + "`n")
"canonical history schema: $($members.Count) lines (classes and data members)" | Out-File $o -Append
foreach ($l in $members) { "  schema $l" | Out-File $o -Append }
"P18_CMD_CANONICAL_HISTORY_SCHEMA_FINGERPRINT $((& $git hash-object "$s\schema-cmd.txt").Trim())" | Out-File $o -Append

# The undo/redo policy: the three framework-facing operations of the edit base and of the create command.
$policy = (BodyOf $cpp 'Result<void> ThermalAnalysisEditCommand::execute(Document& document) {') +
          (BodyOf $cpp 'Result<void> ThermalAnalysisEditCommand::undo(Document& document) {') +
          (BodyOf $cpp 'Result<void> ThermalAnalysisEditCommand::redo(Document& document) {') +
          (BodyOf $cpp 'Result<void> CreateThermalAnalysisCommand::execute(Document& document) {') +
          (BodyOf $cpp 'Result<void> CreateThermalAnalysisCommand::undo(Document& document) {') +
          (BodyOf $cpp 'Result<void> CreateThermalAnalysisCommand::redo(Document& document) {')
[IO.File]::WriteAllText("$s\undo-redo-policy-cmd.txt", $policy)
"undo/redo policy: edit execute/undo/redo + create execute/undo/redo (bytes $((Get-Item "$s\undo-redo-policy-cmd.txt").Length))" | Out-File $o -Append
"P18_CMD_UNDO_REDO_POLICY_FINGERPRINT $((& $git hash-object "$s\undo-redo-policy-cmd.txt").Trim())" | Out-File $o -Append

# The source-invalidation policy: the one write path (apply -> modifyObject -> setDefinition) and the measured granularity matrix.
$apply = BodyOf $cpp '[[nodiscard]] Result<bool> apply(Document& document, ThermalAnalysisId analysis,'
$matrix = ([regex]::Matches([IO.File]::ReadAllText("tests/thermal/ThermalCommandCurrentnessTests.cpp"), 'constexpr Layers k\w+Edit\{[^}]*\};') | ForEach-Object { $_.Value }) -join "`n"
[IO.File]::WriteAllText("$s\invalidation-cmd.txt", $apply + $matrix + "`n")
"source-invalidation policy: apply() and the granularity matrix (bytes $((Get-Item "$s\invalidation-cmd.txt").Length))" | Out-File $o -Append
foreach ($l in ($matrix -split "`n")) { "  matrix $l" | Out-File $o -Append }
"P18_CMD_SOURCE_INVALIDATION_POLICY_FINGERPRINT $((& $git hash-object "$s\invalidation-cmd.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckThermalCommandOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'thermalcmd-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-cmd.txt", (($own) -join "`n") + "`n")
foreach ($l in $own) { "ownership   $l" | Out-File $o -Append }
"THERMAL_CMD_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-cmd.txt").Trim())" | Out-File $o -Append

$d1 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d2 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d3 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-cmd.txt", "src/thermal/CMakeLists.txt     $d1`nCheckLayering.cmake           $d2`nCheckTargetDependencies.cmake $d3`n")
"src/thermal/CMakeLists.txt     $d1" | Out-File $o -Append
"CheckLayering.cmake           $d2" | Out-File $o -Append
"CheckTargetDependencies.cmake $d3" | Out-File $o -Append
"ADR_055_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-cmd.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD): every thermal, meshing, structural, linalg, core and
# renderer file, and the apps -- except the one CMake list that gained the new source.
$untouched = @()
foreach ($d in 'src/thermal', 'include/bettercad/thermal', 'src/meshing', 'include/bettercad/meshing', 'src/structural', 'include/bettercad/structural', 'src/linalg', 'include/bettercad/linalg', 'src/core', 'include/bettercad/core', 'src/renderer', 'include/bettercad/renderer', 'apps') {
    foreach ($f in (Get-ChildItem -File -Recurse $d | Sort-Object FullName)) { $untouched += $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/') }
}
$equal = 0
$different = 0
$new = 0
$list = @()
foreach ($b in $untouched) {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse --verify --quiet "HEAD:$b" 2>$null)   # without --verify an absent path is echoed back as if it were a hash
    if ($head) { $head = $head.Trim() } else { $head = $null }
    if ($null -eq $head) { $new++; $list += "NEW       $b" }
    elseif ($work -eq $head) { $equal++ }
    else { $different++; $list += "DIFFERENT $b" }
}
"untouched-scope files: $($untouched.Count) equal $equal different $different new $new" | Out-File $o -Append
foreach ($b in $list) { "  $b" | Out-File $o -Append }
foreach ($b in 'include/bettercad/thermal/ThermalCommands.hpp', 'src/thermal/ThermalCommands.cpp', 'tests/thermal/ThermalCommandTests.cpp', 'tests/thermal/ThermalCommandCurrentnessTests.cpp', 'tests/thermal/ThermalCommandTestSupport.hpp', 'tests/compile_fail/ThermalCommandMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
