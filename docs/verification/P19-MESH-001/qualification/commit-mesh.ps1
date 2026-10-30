# P19-MESH-001 commit: recompute the eight-path fingerprint, require it equal to
# the frozen one (read from freeze-mesh-pre.txt), refuse placeholders and a BOM,
# commit, and read the commit's subtree hashes back.
# THE SCRATCH DIRECTORY MUST BE THE ONE freeze-mesh.ps1 WROTE TO, and must be durable:
# this pointed at an agent session's temp scratchpad, deleted when the session ends.
# A missing freeze file then surfaced as "freeze file does not carry eight hashes",
# a misleading message for a file that is not there, reached only AFTER the run. It
# now names the real cause and refuses before touching the index (exit 6).
# The commit message had the same defect and no guard at all: exit 7 when it is
# missing, exit 8 when it is empty or whitespace only.
param(
    [string]$Scratch = "C:\Users\hasib\bcdev\p19-mesh-scratch",
    [string]$RootDir = "C:\Users\hasib\OneDrive\Desktop\CAD",
    [string]$GitExe = ""
)
if ($GitExe -eq "") {
    $GitExe = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
    if (-not (Test-Path -LiteralPath $GitExe)) { $GitExe = "git" }
}
$git = $GitExe
$s = $Scratch
Set-Location $RootDir
if (-not (Test-Path -LiteralPath "$s\freeze-mesh-pre.txt")) {
    "NO FREEZE FILE at $s\freeze-mesh-pre.txt -- not committing."
    "Run freeze-mesh.ps1 -Out freeze-mesh-pre.txt with the SAME -Scratch first."
    exit 6
}
# The commit message is the OTHER input this script cannot produce, and it was
# unguarded. Get-Content on a missing file raises a NON-TERMINATING error, so
# $bytes stayed $null, `$null -eq 239` is false, and the BOM check below reported
# a pass while reading nothing at all. The 2026-10-29 rehearsal recorded exactly
# that -- "passed the BOM guard" with no message file in the scratch directory --
# and the real failure would have arrived later, at `git commit -F` on a path that
# does not exist, with no named exit, AFTER a seven-hour run. An empty or
# whitespace-only file fails the same way: git aborts on an empty commit message.
# Both are checked here, before the index is touched, as the freeze file is above.
if (-not (Test-Path -LiteralPath "$s\commit-message-mesh.txt")) {
    "NO COMMIT MESSAGE at $s\commit-message-mesh.txt -- not committing."
    "Write the milestone's commit message there first, with the SAME -Scratch."
    exit 7
}
if (-not ((Get-Content -LiteralPath "$s\commit-message-mesh.txt" -Raw) -match '\S')) {
    "COMMIT MESSAGE IS EMPTY at $s\commit-message-mesh.txt -- not committing."
    "git aborts on an empty message; write the real one before running this."
    exit 8
}
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$frozen = @{}
foreach ($line in Get-Content "$s\freeze-mesh-pre.txt") {
    if ($line -match '^(apps|include|src|tests|examples|cmake|CMakeLists\.txt|CMakePresets\.json)\s+([0-9a-f]{40})$') {
        $frozen[$Matches[1]] = $Matches[2]
    }
}
if ($frozen.Count -ne 8) { "freeze file does not carry eight hashes -- not committing"; exit 5 }
$env:GIT_INDEX_FILE = "$s\scratch-index-mesh-commit"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
"recomputed before the commit (scratch tree $tree):"
$ok = $true
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    $match = if ($h -eq $frozen[$p]) { "== frozen" } else { "!= frozen $($frozen[$p])"; $ok = $false }
    "  $p $h $match"
}
if (-not $ok) { "FINGERPRINT MISMATCH -- not committing"; exit 2 }
$bytes = Get-Content "$s\commit-message-mesh.txt" -Encoding Byte -TotalCount 3
"message first bytes: " + ($bytes -join ",")
if ($bytes[0] -eq 239) { "BOM present -- not committing"; exit 3 }
# -CaseSensitive, because Select-String is case-INSENSITIVE by default and was
# matching the ordinary lowercase word "placeholders" in README.md's own revision
# prose ("the placeholders replaced by the true state"). The marker this guard exists
# for is the uppercase PLACEHOLDER; a lowercase mention of the concept is never one.
# Without the flag the guard refuses AFTER a passing seven-hour run, pointing at a
# sentence that is not a placeholder, and no amount of editing the three real markers
# clears it. Verified: 3 real markers still caught, the prose line no longer matched.
$placeholders = Select-String -CaseSensitive -Path "docs\verification\P19-MESH-001\*.md", "$s\commit-message-mesh.txt" -Pattern "PLACEHOLDER"
if ($placeholders) { "PLACEHOLDERS REMAIN -- not committing"; $placeholders | ForEach-Object { $_.Path + ":" + $_.LineNumber }; exit 4 }
# EVERY qualified path, not a hand list: P19-FLUID-001's first commit used "docs include src tests" and left apps/ out; the post-commit check caught it. The paths come from $paths.
& $git add -- ROADMAP.md TODO.md docs $paths 2>$null
"untracked left behind:"
& $git status --short | Where-Object { $_ -match '^\?\?' }
& $git commit -q -F "$s\commit-message-mesh.txt"
$head = (& $git rev-parse HEAD).Trim()
& $git log -1 --format="%H %s"
"subtree hashes of the commit:"
$ok2 = $true
foreach ($p in $paths) {
    $h = (& $git rev-parse "${head}^{tree}:$p").Trim()
    $match = if ($h -eq $frozen[$p]) { "== frozen" } else { "!= frozen"; $ok2 = $false }
    "  $p $h $match"
}
if ($ok2) { "ALL EIGHT == FROZEN" } else { "MISMATCH AFTER COMMIT" }
& $git status --short
& $git show --stat=100 --format= HEAD | Select-Object -Last 1
