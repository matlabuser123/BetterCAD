# P18-QUAL-001 mutation campaign driver (ASCII only). Each probe edits one production file, rebuilds bettercad_tests (release-ext), runs every
# thermal suite plus the final-gate cases, and restores the file (mutate-ref.ps1 with -Filter). A literal backslash-n in Find/Replace stands for a newline.
# PER_DRYRUN=1 only counts occurrences of each Find text.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$el = "$root\src\thermal\ThermalElement.cpp"
$sys = "$root\src\thermal\ThermalSystem.cpp"
$lv = "$root\src\thermal\ThermalLoadVector.cpp"
$post = "$root\src\thermal\ThermalPost.cpp"
$solve = "$root\src\thermal\ThermalSolve.cpp"
$lin = "$root\src\linalg\SymmetricSolve.cpp"
$val = "$root\src\thermal\ThermalValidation.cpp"
$dofh = "$root\include\bettercad\thermal\ThermalDof.hpp"
$dofc = "$root\src\thermal\ThermalDof.cpp"
$tgt = "$root\src\thermal\ThermalTarget.cpp"
$mat = "$root\src\thermal\ThermalMaterial.cpp"
$trn = "$root\src\thermal\ThermalTransient.cpp"
$conv = "$root\src\thermal\ThermalConvection.cpp"
$cmd = "$root\src\thermal\ThermalCommands.cpp"
$json = "$root\src\io\json\ThermalAnalysisJson.cpp"
$cli = "$root\apps\bettercad_cli\ThermalReports.cpp"
$occt = "$root\src\renderer\occt\OcctOverlayPresentation.cpp"
$bal = "$root\src\thermal\ThermalBalance.cpp"
$progress = "$s\qual-mutation-progress.txt"
$only = $args
$probes = @(
  @{ Id='QP16'; File=$mat; Desc='a steady analysis demands rho and cp (the mode-specific requirement is lost)'; Find='if (needsDensity || needsSpecificHeat) {'; Replace='if (true || needsDensity || needsSpecificHeat) {' },
  @{ Id='QP23'; File=$occt; Desc='the viewer sends metres to the kernel without the millimetre conversion'; Find='return geometry::occt::toModel(Length::fromSi(metres));'; Replace='return metres;' },
 @{ Id='QP26'; File=$solve; Desc='the nonzero Dirichlet coupling term Kfc Tc is dropped from the reduced right-hand side'; Find='out.beff_[equation] = out.qf_[equation] - coupled;'; Replace='out.beff_[equation] = out.qf_[equation] - 0.0 * coupled;' },
 @{ Id='QP27'; File=$dofc; Desc='two different prescribed temperatures on one unknown are silently merged (first wins)'; Find='if (unique.back().temperature == constraint.temperature) {\n                continue;\n            }'; Replace='if (true) {\n                continue;\n            }' },
 @{ Id='QP28'; File=$bal; Desc='the heat-balance tolerance is a thousand times looser'; Find='return std::abs(r) <= kBalanceAbsoluteToleranceW + kBalanceRelativeTolerance * (std::abs(g) + std::abs(a));'; Replace='return std::abs(r) <= 1000.0 * (kBalanceAbsoluteToleranceW + kBalanceRelativeTolerance * (std::abs(g) + std::abs(a)));' }
)
if ($only.Count -gt 0) { $probes = $probes | Where-Object { $only -contains $_.Id } }
foreach ($p in $probes) {
    $find = $p.Find.Replace('\n', "`n")
    $replace = $p.Replace.Replace('\n', "`n")
    if ($env:PER_DRYRUN) {
        $t = [IO.File]::ReadAllText($p.File)
        $n = ([regex]::Matches($t, [regex]::Escape($find))).Count
        "{0} occurrences={1}  {2}" -f $p.Id, $n, $p.Desc
        continue
    }
    $before = [IO.File]::ReadAllText($p.File)
    $started = Get-Date
    [IO.File]::WriteAllText("$s\find-$($p.Id).txt", $find, (New-Object Text.UTF8Encoding $false))
    [IO.File]::WriteAllText("$s\replace-$($p.Id).txt", $replace, (New-Object Text.UTF8Encoding $false))
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-ref.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -TimeoutSeconds 1500 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $cases = if ($log -match 'FAILED CASES : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | {4} | {5} | {6:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $cases, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
if (-not $env:PER_DRYRUN) { Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8 }
