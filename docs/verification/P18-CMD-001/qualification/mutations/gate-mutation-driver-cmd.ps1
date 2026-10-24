# P18-CMD-001 gate mutation campaign. The ownership gate is a CMake script that ctest runs against the real tree and against 14 fixtures (the
# fixtures that must FAIL are registered with WILL_FAIL-style expectations). A rule that is disabled must make one of the 15 tests fail; if none
# does, the rule is not pinned by a fixture. Each probe edits the gate, runs the architecture.thermalcmd tests (no rebuild is needed: the script is
# read when the test runs) and restores the gate.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$gate = "$root\tests\architecture\CheckThermalCommandOwnership.cmake"
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$progress = "$s\cmd-gate-mutation-progress.txt"
$probes = @(
 @{ Id='GATE01'; Desc='rule 1: a quoted include is allowed'; Find='if(_code MATCHES "#include[ \t]*\"([^\"]+)\"")'; Replace='if(FALSE)' },
 @{ Id='GATE02'; Desc='rule 1: any bettercad header may be included'; Find='if(_path MATCHES "^bettercad/" AND NOT _path MATCHES "^(${_allowed_includes})")'; Replace='if(FALSE)' },
 @{ Id='GATE03'; Desc='rule 1: a third-party header may be included'; Find='elseif(_path MATCHES "(Eigen|occt|OCC|Qt|QtCore|opencascade)")'; Replace='elseif(FALSE)' },
 @{ Id='GATE04'; Desc='rule 2: derived work may be named'; Find='if(_code MATCHES "(${_derived})")'; Replace='if(FALSE)' },
 @{ Id='GATE05'; Desc='rule 3: a mesh identity may be named'; Find='if(_code MATCHES "(${_mesh_identity})")'; Replace='if(FALSE)' },
 @{ Id='GATE06'; Desc='rule 4: a record may be addressed by position'; Find='if(_code MATCHES "(${_positional})")'; Replace='if(FALSE)' },
 @{ Id='GATE07'; Desc='rule 5: a number may be parsed from text'; Find='if(_code MATCHES "(${_text_round_trip})")'; Replace='if(FALSE)' },
 @{ Id='GATE08'; Desc='rule 6: a second authority / non-deterministic construct is allowed'; Find='if(_code MATCHES "(${_authority})")'; Replace='if(FALSE)' },
 @{ Id='GATE09'; Desc='rule 7: the one path need not be used'; Find='if(NOT _code MATCHES "${_name}")'; Replace='if(FALSE)' },
 @{ Id='GATE10'; Desc='rule 8: a second owner of thermal commands is allowed'; Find='if(_code MATCHES "(public[ \t]+Command[ \t]*[{,]|ThermalAnalysisEditCommand)")'; Replace='if(FALSE)' },
 @{ Id='GATE11'; Desc='rule 8: nobody is checked for editing around the commands'; Find='elseif(_file MATCHES "^(apps/|src/|include/bettercad/renderer/)")'; Replace='elseif(FALSE)' },
 @{ Id='GATE12'; Desc='rule 8: the scope falls back to the renderer only'; Find='elseif(_file MATCHES "^(apps/|src/|include/bettercad/renderer/)")'; Replace='elseif(_file MATCHES "^(apps/|src/renderer/|include/bettercad/renderer/)")' },
 @{ Id='GATE13'; Desc='rule 8: a qualified modifyObject<bettercad::thermal::ThermalAnalysis> is not seen'; Find='modifyObject[ \t]*<[^>]*ThermalAnalysis[ \t]*>'; Replace='modifyObject[ \t]*<(thermal::)?ThermalAnalysis[ \t]*>' },
 @{ Id='GATE14'; Desc='rule 8: a direct setDefinition call is not seen'; Find='|->[ \t]*setDefinition[ \t]*\\(|\\.[ \t]*setDefinition[ \t]*\\()'; Replace=')' }
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
        $out = & ctest --test-dir "C:\Users\hasib\bcdev\bc-build\debug-ext" -R "architecture.thermalcmd" 2>&1
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
