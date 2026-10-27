# P19-FLUID-001 commit: recompute the eight-path fingerprint, require it equal to
# the frozen one (read from freeze-fluid-pre.txt), refuse placeholders and a BOM,
# commit, and read the commit's subtree hashes back.
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$frozen = @{}
foreach ($line in Get-Content "$s\freeze-fluid-pre.txt") {
    if ($line -match '^(apps|include|src|tests|examples|cmake|CMakeLists\.txt|CMakePresets\.json)\s+([0-9a-f]{40})$') {
        $frozen[$Matches[1]] = $Matches[2]
    }
}
if ($frozen.Count -ne 8) { "freeze file does not carry eight hashes -- not committing"; exit 5 }
$env:GIT_INDEX_FILE = "$s\scratch-index-fluid-commit"
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
$bytes = Get-Content "$s\commit-message-fluid.txt" -Encoding Byte -TotalCount 3
"message first bytes: " + ($bytes -join ",")
if ($bytes[0] -eq 239) { "BOM present -- not committing"; exit 3 }
$placeholders = Select-String -Path "docs\verification\P19-FLUID-001\*.md", "$s\commit-message-fluid.txt" -Pattern "PLACEHOLDER"
if ($placeholders) { "PLACEHOLDERS REMAIN -- not committing"; $placeholders | ForEach-Object { $_.Path + ":" + $_.LineNumber }; exit 4 }
& $git add -- ROADMAP.md TODO.md docs include src tests 2>$null
"untracked left behind:"
& $git status --short | Where-Object { $_ -match '^\?\?' }
& $git commit -q -F "$s\commit-message-fluid.txt"
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
