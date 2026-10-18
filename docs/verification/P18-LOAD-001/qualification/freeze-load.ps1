# P18-LOAD-001 freeze: the eight-path fingerprint from a scratch index, exactly
# as qualify.cmd computes it.
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-load"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$out = "$s\freeze-load.txt"
"P18-LOAD-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $out -Append
"scratch tree $tree" | Out-File $out -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $out -Append
}
"status:" | Out-File $out -Append
& $git status --short | Out-File $out -Append
Get-Content $out
