# P18-PERSIST-001 gate mutation campaign. The ownership gate is a CMake script that ctest runs against the real tree and against 14 fixtures (the
# fixtures that must FAIL are registered with WILL_FAIL-style expectations). A rule that is disabled must make one of the 15 tests fail; if none
# does, the rule is not pinned by a fixture. Each probe edits the gate, runs the architecture.thermalpersist tests (no rebuild is needed: the script is
# read when the test runs) and restores the gate.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$gate = "$root\tests\architecture\CheckThermalPersistenceOwnership.cmake"
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$progress = "$s\persist-gate-mutation-progress.txt"
$probes = @(
 @{ Id='PGATE01'; Desc='rule 1: a derived-chain header may be included'; Find='if(_path MATCHES "^(${_forbidden_includes})" OR _path MATCHES "(${_forbidden_includes})")'; Replace='if(FALSE)' },
 @{ Id='PGATE02'; Desc='rule 1: a third-party header may be included'; Find='elseif(_path MATCHES "(Eigen|occt|OCC|Qt|QtCore|opencascade)")'; Replace='elseif(FALSE)' },
 @{ Id='PGATE03'; Desc='rule 1: any quoted project header may be included'; Find='elseif(_inc MATCHES "\"" AND NOT _path MATCHES "^(${_allowed_project_includes})$")'; Replace='elseif(FALSE)' },
 @{ Id='PGATE04'; Desc='rule 1: a space after the # hides an include'; Find='string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" _includes "${_code}")'; Replace='string(REGEX MATCHALL "#include[ \t]*[<\"][^>\"]+[>\"]" _includes "${_code}")' },
 @{ Id='PGATE05'; Desc='rule 2: derived work may be named'; Find='if(_code MATCHES "(${_derived})")'; Replace='if(FALSE)' },
 @{ Id='PGATE06'; Desc='rule 3: a mesh identity may be named'; Find='if(_code MATCHES "(${_mesh_identity})")'; Replace='if(FALSE)' },
 @{ Id='PGATE07'; Desc='rule 4: a material property may be named'; Find='if(_code MATCHES "(${_material})")'; Replace='if(FALSE)' },
 @{ Id='PGATE08'; Desc='rule 5: a number may be parsed from text'; Find='if(_code MATCHES "(${_text_parse})")'; Replace='if(FALSE)' },
 @{ Id='PGATE09'; Desc='rule 6: a non-deterministic construct is allowed'; Find='if(_code MATCHES "(${_nondeterminism})")'; Replace='if(FALSE)' },
 @{ Id='PGATE10'; Desc='rule 9: the reader may resolve and look up'; Find='if(_code MATCHES "(${_resolution})")'; Replace='if(FALSE)' },
 @{ Id='PGATE11'; Desc='rule 10: a unit may be converted'; Find='if(_code MATCHES "(${_units})")'; Replace='if(FALSE)' },
 @{ Id='PGATE12'; Desc='rule 7: the owners need not be used at every place that needs them'; Find='if(_used LESS _minimum)'; Replace='if(FALSE)' },
 @{ Id='PGATE13'; Desc='rule 7: the strict-key reads are counted as one'; Find='"requireObject=5"'; Replace='"requireObject=1"' },
 @{ Id='PGATE14'; Desc='rule 8: another thermal header may be included in the io module'; Find='if(NOT _inc MATCHES "bettercad/thermal/ThermalAnalysisObject\\.hpp[>\"]" OR _inc MATCHES "bettercad/thermal/[A-Za-z0-9_/]+/ThermalAnalysisObject")'; Replace='if(FALSE)' },
 @{ Id='PGATE15'; Desc='rule 8: a lookalike of the analysis header is allowed (unanchored)'; Find='if(NOT _inc MATCHES "bettercad/thermal/ThermalAnalysisObject\\.hpp[>\"]" OR _inc MATCHES "bettercad/thermal/[A-Za-z0-9_/]+/ThermalAnalysisObject")'; Replace='if(NOT _inc MATCHES "ThermalAnalysisObject\\.hpp")' },
 @{ Id='PGATE16'; Desc='rule 8: the dispatch may name derived work'; Find='if(_other MATCHES "src/io/json/(DocumentJson\\.cpp|ObjectJson\\.hpp)" AND _other_code MATCHES "(${_derived})")'; Replace='if(FALSE)' }
)
$only = $args
if ($only.Count -gt 0) { $probes = $probes | Where-Object { $only -contains $_.Id } }
Set-Location $root
foreach ($p in $probes) {
    $original = [IO.File]::ReadAllText($gate)
    if (-not $original.Contains($p.Find)) { Add-Content $progress "$($p.Id) | $($p.Desc) | NOT APPLIED (find text missing)"; continue }
    $started = Get-Date
    [IO.File]::WriteAllText($gate, $original.Replace($p.Find, $p.Replace), (New-Object Text.UTF8Encoding $false))
    try {
        $out = & ctest --test-dir "C:\Users\hasib\bcdev\bc-build\debug-ext" -R "architecture.thermalpersist" 2>&1
    } finally {
        [IO.File]::WriteAllText($gate, $original, (New-Object Text.UTF8Encoding $false))
    }
    $summary = ($out | Select-String "tests passed|tests failed out of").Line -join " "
    $failed = ($out | Select-String "\*\*\*Failed|\(Failed\)|Failed  ").Line | ForEach-Object { ($_ -split '\s+' | Where-Object { $_ -match '^architecture\.' }) } | Select-Object -Unique
    $verdict = if ($summary -match '100% tests passed') { 'SURVIVED' } else { 'KILLED' }
    $restored = if ([IO.File]::ReadAllText($gate) -ceq $original) { 'restored' } else { 'NOT RESTORED' }
    Add-Content $progress ("{0} | {1} | {2} | {3} | by: {4} | {5} | {6:N0}s" -f $p.Id, $p.Desc, $verdict, $summary, ($failed -join ', '), $restored, ((Get-Date) - $started).TotalSeconds)
}
Add-Content $progress "gate campaign finished $(Get-Date -Format s)"
