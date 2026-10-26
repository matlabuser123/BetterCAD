# P18-REFMOD-001 mutation campaign driver. Each probe edits the named file, rebuilds bettercad_tests (release-ext), runs the thermal
# reference suite and restores the file (mutate-ref.ps1). A literal backslash-n in Find/Replace stands for a newline (sources are LF).
# PER_DRYRUN=1 only counts occurrences of each Find text.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$post = "$root\src\thermal\ThermalPost.cpp"
$loadv = "$root\src\thermal\ThermalLoadVector.cpp"
$conv = "$root\src\thermal\ThermalConvection.cpp"
$elem = "$root\src\thermal\ThermalElement.cpp"
$trans = "$root\src\thermal\ThermalTransient.cpp"
$solve = "$root\src\thermal\ThermalSolve.cpp"
$valid = "$root\src\thermal\ThermalValidation.cpp"
$bal = "$root\src\thermal\ThermalBalance.cpp"
$target = "$root\src\thermal\ThermalTarget.cpp"
$analysis = "$root\src\thermal\ThermalAnalysis.cpp"
$models = "$root\examples\reference_models\ThermalModels.cpp"
$oracle = "$root\tests\reference\ThermalAnalytic.hpp"
$progress = "$s\ref-mutation-progress.txt"
$only = $args
$probes = @(
 # ---- production: a physical law or a sign moved
 @{ Id='RP01'; File=$post; Desc='Fourier: the x component of the heat flux loses its minus sign'; Find='.x = HeatFlux{} - conductivity * gradient.x,'; Replace='.x = HeatFlux{} + conductivity * gradient.x,' },
 @{ Id='RP02'; File=$loadv; Desc='volumetric generation is shared among 3 nodes instead of 4'; Find='generation.si() * (volume.si() / 4.0)'; Replace='generation.si() * (volume.si() / 3.0)' },
 @{ Id='RP03'; File=$conv; Desc='the convection stiffness is h A / 6 instead of h A / 12'; Find='coefficient.si() * (area.si() / 12.0)'; Replace='coefficient.si() * (area.si() / 6.0)' },
 @{ Id='RP04'; File=$conv; Desc='the convection load sees an ambient 5 K warmer'; Find='(coefficient.si() * (area.si() / 3.0)) * ambient.si();'; Replace='(coefficient.si() * (area.si() / 3.0)) * (ambient.si() + 5.0);' },
 @{ Id='RP05'; File=$elem; Desc='the consistent capacity off-diagonal is rho cp V / 10 instead of /20'; Find='rhoCpV / 20.0'; Replace='rhoCpV / 10.0' },
 @{ Id='RP06'; File=$trans; Desc='the partial last step is a full nominal step'; Find='grid.remainder_ = endTime - full * dt;'; Replace='grid.remainder_ = dt;' },
 @{ Id='RP07'; File=$trans; Desc='the final stored time is the nominal grid time, not the end time'; Find='    if (index >= steps_) {\n        return end_;\n    }\n    return static_cast<double>(index) * nominal_;'; Replace='    return static_cast<double>(index) * nominal_;' },
 @{ Id='RP08'; File=$solve; Desc='every row is declared anchored (the datum check is disabled)'; Find='anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;'; Replace='anchoredRow[row] = (std::abs(sum) > kAnchorTolerance * absolute) || true;' },
 @{ Id='RP09'; File=$valid; Desc='the validator never reports a stale derived object'; Find='        if (!current.has_value()) {\n            ValidationFinding finding = make(ValidationCode::SourceStale, current.error().message);'; Replace='        if (false) {\n            ValidationFinding finding = make(ValidationCode::SourceStale, current.error().message);' },
 @{ Id='RP10'; File=$bal; Desc='the reaction of a prescribed unknown has the opposite sign'; Find='.heatInto = Power::fromSi(heat)});'; Replace='.heatInto = Power::fromSi(-heat)});' },
 @{ Id='RP11'; File=$bal; Desc='the heat through a convecting face has the opposite sign'; Find='.heatInto = Power::fromSi(-out)};'; Replace='.heatInto = Power::fromSi(out)};' },
 @{ Id='RP12'; File=$target; Desc='an unresolved face reference is not reported as unresolved'; Find='    if (!set->fullyResolved()) {\n        out.problem = TargetProblem::Unresolved;'; Replace='    if (false) {\n        out.problem = TargetProblem::Unresolved;' },
 @{ Id='RP13'; File=$analysis; Desc='a stale mesh is accepted at the model gate'; Find='    if (!meshing::describesTheModel(currency)) {'; Replace='    if (false) {' },
 @{ Id='RP14'; File=$elem; Desc='the conduction matrix is 0.1 percent too stiff (tolerance sensitivity)'; Find='const double kv = conductivity.si() * geometry.volume().si();'; Replace='const double kv = conductivity.si() * geometry.volume().si() * 1.001;' },
 # ---- the reference models: a document that does not say what its test thinks it says
 @{ Id='RM01'; File=$models; Desc='the slab holds its caps the wrong way round'; Find='heldCaps(m, d, kSlabHotKelvin, kSlabColdKelvin);'; Replace='heldCaps(m, d, kSlabColdKelvin, kSlabHotKelvin);' },
 @{ Id='RM02'; File=$models; Desc='the convection model sits in an ambient 10 K warmer than declared'; Find='kConvectionAmbientKelvin)};'; Replace='kConvectionAmbientKelvin + 10.0)};' },
 @{ Id='RM03'; File=$models; Desc='the lumped cube leaves one face insulated'; Find=',\n                                        convecting(6U, m.side(3), kLumpedCoefficient, kLumpedAmbientKelvin)};'; Replace='};' },
 @{ Id='RM04'; File=$models; Desc='the generation box generates twice the declared heat'; Find='d.loads = {generation(1U, kGenerationWattsPerM3)};'; Replace='d.loads = {generation(1U, 2.0 * kGenerationWattsPerM3)};' },
 @{ Id='RM05'; File=$models; Desc='the "unresolved" held temperature targets a real face'; Find='held(3U, m.missing(), 350.0)'; Replace='held(3U, m.side(0), 350.0)' },
 @{ Id='RM06'; File=$models; Desc='the "singular" model is given a held temperature (so it is not singular)'; Find='.intent = [](const ThermalBlockModel&, ThermalAnalysisDefinition& d) {\n                                    d.mode = ThermalAnalysisMode::Steady;\n                                    d.loads = {generation(1U, 1.0e5)};'; Replace='.intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {\n                                    d.mode = ThermalAnalysisMode::Steady;\n                                    d.loads = {generation(1U, 1.0e5)};\n                                    d.boundaryConditions = {held(1U, m.start(), 300.0)};' },
 @{ Id='RM07'; File=$models; Desc='the "balanced" model is not balanced (a net heat in)'; Find='surfaceFlux(2U, m.end(), -1000.0)'; Replace='surfaceFlux(2U, m.end(), -500.0)' },
 # ---- the oracle: an expected value that stopped being the physics
 @{ Id='RO01'; File=$oracle; Desc='the slab flux uses Th + Tc'; Find='return conductivity * (hot - cold) / length;'; Replace='return conductivity * (hot + cold) / length;' },
 @{ Id='RO02'; File=$oracle; Desc='the lumped time constant is halved'; Find='return capacity() / (coefficient * area); }'; Replace='return capacity() / (coefficient * area * 2.0); }' },
 @{ Id='RO03'; File=$oracle; Desc='the series heat rate forgets the convection resistance'; Find='return (hot - ambient) / (conductionResistance() + convectionResistance());'; Replace='return (hot - ambient) / conductionResistance();' },
 @{ Id='RO04'; File=$oracle; Desc='the independent FE stiffness is half what it should be'; Find='ke[i][j] = conductivity * v * dot(g[i], g[j]);'; Replace='ke[i][j] = conductivity * v * dot(g[i], g[j]) * 0.5;' },
 @{ Id='RO05'; File=$oracle; Desc='the oracle header includes a BetterCAD header'; Find='#include <vector>\n\nnamespace bettercad::test::analytic::heat {'; Replace='#include <vector>\n#include <bettercad/meshing/Mesh.hpp>\n\nnamespace bettercad::test::analytic::heat {' },
 # ---- added after the independent review: row-sum-preserving matrix distributions and the surface load
 @{ Id='RP15'; File=$loadv; Desc='a surface heat flux is shared among 2 nodes instead of 3'; Find='flux.si() * (area.si() / 3.0)'; Replace='flux.si() * (area.si() / 2.0)' },
 @{ Id='RP16'; File=$elem; Desc='the consistent capacity matrix is replaced by a lumped (diagonal) one with the same row sums'; Find='const double offDiagonal = rhoCpV / 20.0;\n    const double diagonal = 2.0 * offDiagonal;'; Replace='const double offDiagonal = 0.0;\n    const double diagonal = rhoCpV / 4.0;' },
 @{ Id='RP17'; File=$conv; Desc='the Robin face matrix is replaced by a lumped (diagonal) one with the same row sums'; Find='const double diagonal = 2.0 * unit;\n    const double off = unit;'; Replace='const double diagonal = 4.0 * unit;\n    const double off = 0.0;' }
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
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-ref.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -TimeoutSeconds 900 | Out-Null
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
