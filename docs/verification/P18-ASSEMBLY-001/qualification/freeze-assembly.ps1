# P18-ASSEMBLY-001 freeze: the eight-path fingerprint from a scratch index, exactly
# as qualify.cmd computes it, and the thermal-assembly contract fingerprint.
param([string]$Out = "freeze-assembly.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-as"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
Remove-Item Env:\GIT_INDEX_FILE
$o = "$s\$Out"
"P18-ASSEMBLY-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}
$h1 = (& $git hash-object include/bettercad/thermal/ThermalSystem.hpp).Trim()
$h2 = (& $git hash-object include/bettercad/thermal/ThermalSparse.hpp).Trim()
$lines = "ThermalSystem.hpp   $h1`nThermalSparse.hpp   $h2`n"
$tmp = "$s\contract-as.txt"
[IO.File]::WriteAllText($tmp, $lines)
"ThermalSystem.hpp   $h1" | Out-File $o -Append
"ThermalSparse.hpp   $h2" | Out-File $o -Append
"P18_THERMAL_ASSEMBLY_CONTRACT_FINGERPRINT $((& $git hash-object $tmp).Trim())" | Out-File $o -Append
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o