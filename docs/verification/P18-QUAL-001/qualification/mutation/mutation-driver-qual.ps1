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
$progress = "$s\qual-mutation-progress.txt"
$only = $args
$probes = @(
 @{ Id='QP01'; File=$el; Desc='an inverted tetrahedron is admitted (the sign of detJ is not checked)'; Find='    if (det < 0.0) {\n        return ElementProblem::InvertedElement;'; Replace='    if (false) {\n        return ElementProblem::InvertedElement;' },
 @{ Id='QP02'; File=$el; Desc='the shape gradients use J^-1 instead of J^-T (a transposed Jacobian)'; Find='sum += jInverse[k][axis] * reference[k];'; Replace='sum += jInverse[axis][k] * reference[k];' },
 @{ Id='QP03'; File=$el; Desc='the consistent capacity matrix is replaced by the one-point rank-1 (centroid) matrix'; Find='const double offDiagonal = rhoCpV / 20.0;\n    const double diagonal = 2.0 * offDiagonal;'; Replace='const double offDiagonal = rhoCpV / 16.0;\n    const double diagonal = offDiagonal;' },
 @{ Id='QP04'; File=$sys; Desc='duplicate sparse conductance contributions OVERWRITE instead of summing'; Find='conductanceValues[*slot] += (*ke)(a, b).si();'; Replace='conductanceValues[*slot] = (*ke)(a, b).si();' },
 @{ Id='QP05'; File=$sys; Desc='the Robin source Qconv is omitted from the heat vector'; Find='heat[row] += facet.term.source(a).si();'; Replace='heat[row] += 0.0 * facet.term.source(a).si();' },
 @{ Id='QP06'; File=$sys; Desc='the prepared heat loads are omitted from the heat vector'; Find='heat[row] += data.heatInput[row].si();'; Replace='heat[row] += 0.0 * data.heatInput[row].si();' },
 @{ Id='QP07'; File=$lv; Desc='the sign of a surface heat flux is flipped (q'''' > 0 no longer heats the body)'; Find='return Power::fromSi(flux.si() * (area.si() / 3.0));'; Replace='return Power::fromSi(-flux.si() * (area.si() / 3.0));' },
 @{ Id='QP08'; File=$post; Desc='the z component of the heat flux is +k grad T (Fourier sign lost)'; Find='.z = HeatFlux{} - conductivity * gradient.z}'; Replace='.z = HeatFlux{} + conductivity * gradient.z}' },
 @{ Id='QP09'; File=$solve; Desc='the anchoring check is disabled (a singular steady model is not refused)'; Find='anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;'; Replace='anchoredRow[row] = (std::abs(sum) > kAnchorTolerance * absolute) || true;' },
 @{ Id='QP10'; File=$lin; Desc='the independent residual gate is skipped'; Find='if (!(norms.normalized <= settings.relativeResidualTolerance)) {'; Replace='if (false) {' },
 @{ Id='QP11'; File=$val; Desc='the validator no longer recomputes the heat-balance tolerance'; Find='const bool within = balanceWithinTolerance(state.residual(), state.grossScale(), state.arithmeticScale);'; Replace='const bool within = true;' },
 @{ Id='QP12'; File=$val; Desc='the validator never reports a stale derived object'; Find='        if (!current.has_value()) {\n            ValidationFinding finding = make(ValidationCode::SourceStale, current.error().message);'; Replace='        if (false) {\n            ValidationFinding finding = make(ValidationCode::SourceStale, current.error().message);' },
 @{ Id='QP13'; File=$dofh; Desc='a DOF map describes any mesh with the same node COUNT (the stamp is not compared)'; Find='return mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size();'; Replace='return mesh.nodes().size() == nodes_.size();' },
 @{ Id='QP14'; File=$tgt; Desc='an unresolved face reference is not reported as unresolved'; Find='    if (!set->fullyResolved()) {\n        out.problem = TargetProblem::Unresolved;'; Replace='    if (false) {\n        out.problem = TargetProblem::Unresolved;' },
 @{ Id='QP15'; File=$tgt; Desc='a target that resolved to zero facets is accepted'; Find='    if (set->facets.empty()) {'; Replace='    if (false) {' },
 @{ Id='QP16'; File=$mat; Desc='a steady analysis demands rho and cp (the mode-specific requirement is lost)'; Find='if (needsDensity || needsSpecificHeat) {'; Replace='if (true || needsDensity || needsSpecificHeat) {' },
 @{ Id='QP17'; File=$trn; Desc='the shortened last step is a full nominal step'; Find='grid.remainder_ = endTime - full * dt;'; Replace='grid.remainder_ = dt;' },
 @{ Id='QP18'; File=$post; Desc='the recovered fields no longer notice that the material was edited'; Find='if (!(model.conductivity() == material->conductivity())) {'; Replace='if (false) {' },
 @{ Id='QP19'; File=$conv; Desc='the Robin stiffness is h A / 6 instead of h A / 12'; Find='coefficient.si() * (area.si() / 12.0)'; Replace='coefficient.si() * (area.si() / 6.0)' },
 @{ Id='QP20'; File=$cmd; Desc='a command the document refuses is reported as executed (it would enter the history)'; Find='        return std::unexpected(changed.error());\n    }\n    changed_ = *changed;'; Replace='        return Result<void>{};\n    }\n    changed_ = *changed;' },
 @{ Id='QP21'; File=$json; Desc='the canonical analysis writer adds a derived key (temperatures)'; Find='json["mode"] = enumName(kModes, d.mode);'; Replace='json["mode"] = enumName(kModes, d.mode);\n    json["temperatures"] = 0;' },
 @{ Id='QP22'; File=$cli; Desc='the CLI exits 0 whatever the verdict'; Find='    return run.accepted() ? ExitCode::Success : ExitCode::Failure;'; Replace='    return ExitCode::Success;' },
 @{ Id='QP23'; File=$occt; Desc='the viewer sends metres to the kernel without the millimetre conversion'; Find='return geometry::occt::toModel(Length::fromSi(metres));'; Replace='return metres;' },
 @{ Id='QP24'; File=$dofc; Desc='the DOF index is the NodeId value, not the dense rank'; Find='static_cast<ThermalDofIndex::ValueType>(found - nodes_.begin())'; Replace='static_cast<ThermalDofIndex::ValueType>(node.value())' },
 @{ Id='QP25'; File=$lv; Desc='a prepared load list is current when the node COUNT agrees (the mesh identity is not compared)'; Find='if (!(prepared.mesh == current.mesh) || prepared.nodeCount != current.nodeCount) {'; Replace='if (prepared.nodeCount != current.nodeCount) {' }
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
