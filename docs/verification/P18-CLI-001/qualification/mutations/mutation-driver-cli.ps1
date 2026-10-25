# P18-CLI-001 mutation campaign driver. Each probe edits the named production file, rebuilds bettercad_tests (debug-ext), runs the thermal CLI
# and run suites AND the ownership gate, and restores the file (mutate-cli.ps1). A probe is KILLED when either the tests or the gate fail.
# A literal backslash-n in Find/Replace stands for a newline (the sources are LF). PER_DRYRUN=1 only counts occurrences of each Find text.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$reports = "$root\apps\bettercad_cli\ThermalReports.cpp"
$edits = "$root\apps\bettercad_cli\ThermalEdits.cpp"
$output = "$root\apps\bettercad_cli\ThermalOutput.hpp"
$runcpp = "$root\src\thermal\ThermalAnalysisRun.cpp"
$runhpp = "$root\include\bettercad\thermal\ThermalAnalysisRun.hpp"
$progress = "$s\cli-mutation-progress.txt"
$only = $args
$probes = @(
 # ---- the reports: a figure moved, swapped or invented
 @{ Id='CR01'; File=$reports; Desc='the temperature minimum and maximum are swapped'; Find='    json.set("min", extremumJson(fin, "node", extrema->low, "K"));\n    json.set("max", extremumJson(fin, "node", extrema->high, "K"));'; Replace='    json.set("min", extremumJson(fin, "node", extrema->high, "K"));\n    json.set("max", extremumJson(fin, "node", extrema->low, "K"));' },
 @{ Id='CR02'; File=$output; Desc='the minimum tie-break is the DESCENDING id'; Find='(candidate.value == e.low.value && candidate.id < e.low.id)'; Replace='(candidate.value == e.low.value && candidate.id > e.low.id)' },
 @{ Id='CR03'; File=$reports; Desc='a node id is the ordinal plus one (NodeId == index + 1)'; Find='        return static_cast<std::uint64_t>(node->value());'; Replace='        return static_cast<std::uint64_t>(i) + 1U;' },
 @{ Id='CR04'; File=$reports; Desc='an element is found by id minus one instead of the stored ordinal'; Find='    if (!ordinal.has_value()) {\n        throw std::logic_error("an element id taken from the result is not in the result");\n    }\n    return *ordinal;'; Replace='    (void)ordinal;\n    return static_cast<std::size_t>(id) - 1U;' },
 @{ Id='CR05'; File=$reports; Desc='the printed heat-flux vector has its x sign inverted'; Find='largest.set("flux", vectorJson(fin, fields.heatFluxes[at].x.si(), fields.heatFluxes[at].y.si(),'; Replace='largest.set("flux", vectorJson(fin, -fields.heatFluxes[at].x.si(), fields.heatFluxes[at].y.si(),' },
 @{ Id='CR06'; File=$reports; Desc='the gradient magnitude is reported squared'; Find='        return normOf(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si());'; Replace='        return normOf(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si()) *\n               normOf(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si());' },
 @{ Id='CR07'; File=$reports; Desc='thermal-validate exits 0 whatever the verdict'; Find='    // warning-only analysis is accepted and exits 0, and this file never names a ValidationStatus enumerator.\n    return run.accepted() ? ExitCode::Success : ExitCode::Failure;'; Replace='    return ExitCode::Success;' },
 @{ Id='CR08'; File=$reports; Desc='thermal-balance exits 0 whatever the verdict'; Find='    // The payload above is the answer; the verdict is the production entry''s.\n    return run.accepted() ? ExitCode::Success : ExitCode::Failure;'; Replace='    return ExitCode::Success;' },
 @{ Id='CR09'; File=$reports; Desc='success means a solution exists (solver success), not that the analysis is accepted'; Find='if (requireAccepted && !ran.run.accepted()) {'; Replace='if (requireAccepted && !ran.run.steady.has_value() && !ran.run.transient.has_value()) {' },
 @{ Id='CR10'; File=$reports; Desc='the result commands never refuse'; Find='if (requireAccepted && !ran.run.accepted()) {'; Replace='if (false && requireAccepted && !ran.run.accepted()) {' },
 @{ Id='CR11'; File=$reports; Desc='a load whose face does not resolve reports faceResolves true'; Find='            entry.set("faceResolves",\n                      JsonValue::boolean(features::checkFaceName(prepared->document, *target).has_value()));'; Replace='            entry.set("faceResolves", JsonValue::boolean(true));' },
 @{ Id='CR12'; File=$reports; Desc='thermal-validate always reports stoppedAt as null'; Find='    structured.set("stoppedAt", run.stoppedAt.has_value() ? JsonValue::text(thermal::toString(*run.stoppedAt))\n                                                          : JsonValue::null());'; Replace='    structured.set("stoppedAt", JsonValue::null());' },
 @{ Id='CR13'; File=$reports; Desc='the stored times are generated as frame times a nominal step'; Find='entry.set("time", s.time.has_value() ? fin.quantity(s.time->si(), "s") : JsonValue::null());'; Replace='entry.set("time", fin.quantity(static_cast<double>(i) * 0.3, "s"));' },
 @{ Id='CR14'; File=$reports; Desc='a progress banner is written to stdout before the solve payload'; Find='    auto ran = analyse("thermal-solve", *prepared, mesher, true, err);'; Replace='    out << "Solving...\n";\n    auto ran = analyse("thermal-solve", *prepared, mesher, true, err);' },
 @{ Id='CR15'; File=$reports; Desc='an extremum value bypasses the finite-checking writer'; Find='    json.set("value", fin.number(e.value));'; Replace='    json.set("value", JsonValue::number(e.value));' },
 @{ Id='CR16'; File=$reports; Desc='the reaction count is off by one'; Find='structured.set("reactionCount", count(last.reactions.size()));'; Replace='structured.set("reactionCount", count(last.reactions.size() + 1U));' },
 @{ Id='CR17'; File=$reports; Desc='a refusal is reported under the first finding of ANY severity'; Find='        if (finding.severity == thermal::ValidationSeverity::Error) {\n            // The CODE'; Replace='        if (true) {\n            // The CODE' },
 @{ Id='CR18'; File=$reports; Desc='the non-finite guard of thermal-solve is removed'; Find='    if (fin.sawNonFinite()) {\n        return refuseNonFinite("thermal-solve", err);\n    }'; Replace='' },
 @{ Id='CR19'; File=$reports; Desc='thermal-validate always reports accepted true'; Find='    structured.set("accepted", JsonValue::boolean(run.accepted()));\n    structured.set("stoppedAt"'; Replace='    structured.set("accepted", JsonValue::boolean(true));\n    structured.set("stoppedAt"' },
 @{ Id='CR20'; File=$reports; Desc='a heat-balance residual is recomputed in the CLI as a sum of the printed terms'; Find='    json.set("residual", fin.quantity(state.residual().si(), "W"));'; Replace='    json.set("residual", fin.quantity(state.terms.appliedSurfaceHeat.si() + state.terms.generatedHeat.si() + state.terms.convectionHeat.si() + state.terms.prescribedTemperatureReaction.si(), "W"));' },
 @{ Id='CR21'; File=$output; Desc='the validation checks are listed in reverse order'; Find='    for (const thermal::ThermalCheck& check : report.checks) {\n        JsonValue entry = JsonValue::object();\n        entry.set("rule"'; Replace='    for (std::size_t reversed = report.checks.size(); reversed > 0U; --reversed) {\n        const thermal::ThermalCheck& check = report.checks[reversed - 1U];\n        JsonValue entry = JsonValue::object();\n        entry.set("rule"' },
 @{ Id='CR22'; File=$reports; Desc='the analysis start time is the first STORED time'; Find='structured.set("startTime", fin.quantity(transient.grid().timeAt(0), "s"));'; Replace='structured.set("startTime", fin.quantity(transient.timeOf(0).si(), "s"));' },
 @{ Id='CR23'; File=$output; Desc='the extremum tie-break is deleted from the maximum'; Find='(candidate.value == e.high.value && candidate.id < e.high.id)'; Replace='(false)' }, # ---- the edits: a value corrected, a rule invented, a refusal swallowed
 @{ Id='CE01'; File=$edits; Desc='a surface flux is made positive on the way in (abs)'; Find='        load = thermal::ThermalLoad{ThermalLoadId::fromValue(*id),\n                                    thermal::SurfaceHeatFluxLoad{.face = *face, .flux = *flux}};'; Replace='        load = thermal::ThermalLoad{ThermalLoadId::fromValue(*id),\n                                    thermal::SurfaceHeatFluxLoad{.face = *face, .flux = HeatFlux::fromSi(std::abs(flux->si()))}};' },
 @{ Id='CE02'; File=$edits; Desc='a convection coefficient is made positive on the way in (abs)'; Find='.coefficient = *coefficient, .ambient = *ambient}};'; Replace='.coefficient = HeatTransferCoefficient::fromSi(std::abs(coefficient->si())), .ambient = *ambient}};' },
 @{ Id='CE03'; File=$edits; Desc='a time step is clamped to a floor'; Find='            settings.timeStep = *value;'; Replace='            settings.timeStep = Time::fromSi(std::max(value->si(), 0.01));' },
 @{ Id='CE04'; File=$edits; Desc='a refused command is reported as success'; Find='    if (auto done = command.execute(document); !done) {'; Replace='    if (auto done = command.execute(document); false && !done) {' },
 @{ Id='CE05'; File=$edits; Desc='a missing id is reported under the duplicate-id code'; Find='            code = "id_not_found";'; Replace='            code = "duplicate_id";' },
 @{ Id='CE06'; File=$edits; Desc='a non-numeric id is read as 0 instead of being a usage error'; Find='        return std::unexpected(\n            malformed(std::format("{} must be a positive integer, not ''{}''", what, text)));'; Replace='        return std::uint64_t{0};' },
 @{ Id='CE07'; File=$edits; Desc='a surface flux without a face is given a default face'; Find='            return std::unexpected(malformed("a surface heat flux needs --face <face>"));'; Replace='            faceText = std::string_view{"face:Extrude001:end_cap"};' },
 @{ Id='CE08'; File=$edits; Desc='a volumetric generation silently accepts a face'; Find='        if (parsed->has("--face")) {\n            return std::unexpected(malformed(\n                "a volumetric heat generation acts on the whole body and takes no --face"));\n        }'; Replace='' },
 @{ Id='CE09'; File=$edits; Desc='an analysis without --mode is transient'; Find='    thermal::ThermalAnalysisDefinition definition;\n    if (const auto mode = parsed->value("--mode")) {'; Replace='    thermal::ThermalAnalysisDefinition definition;\n    definition.mode = thermal::ThermalAnalysisMode::Transient;\n    if (const auto mode = parsed->value("--mode")) {' },
 @{ Id='CE10'; File=$edits; Desc='an unparseable face falls back to the empty reference'; Find='            return std::unexpected(coded("bad_face_reference", face.error()));'; Replace='            face = FaceName{};' },
 @{ Id='CE11'; File=$edits; Desc='a prescribed temperature silently accepts an ambient'; Find='        if (parsed->has("--ambient")) {\n            return std::unexpected(\n                malformed("a prescribed temperature has no ambient; --ambient belongs to --convection"));\n        }'; Replace='' },
 @{ Id='CE12'; File=$edits; Desc='an id above the file cap is accepted by the command line'; Find='    if (value > kLargestFileId) {'; Replace='    if (false) {' },
 @{ Id='CE13'; File=$edits; Desc='time settings no longer need --output (the data model default is taken)'; Find='        if (!parsed->has("--output")) {'; Replace='        if (false) {' },
 @{ Id='CE14'; File=$edits; Desc='a name clash is reported as a duplicate id'; Find='executeEdit(document, command, "duplicate_name")'; Replace='executeEdit(document, command)' }, # ---- the entry
 @{ Id='CK01'; File=$runcpp; Desc='the validator is handed nothing derived'; Find='validateThermalAnalysis(document, regenerator, mesher, analysis, run.derivedState())'; Replace='validateThermalAnalysis(document, regenerator, mesher, analysis, ThermalDerivedState{})' },
 @{ Id='CK02'; File=$runhpp; Desc='every failure is handed to the validator as a solve failure'; Find='failure.has_value() && stoppedAt == ThermalRunStage::Solve ? &*failure : nullptr;'; Replace='failure.has_value() ? &*failure : nullptr;' },
 @{ Id='CK03'; File=$runhpp; Desc='accepted() is a completed chain only (solver success)'; Find='{ return !stoppedAt.has_value() && validation.accepted(); }'; Replace='{ return !stoppedAt.has_value(); }' },
 @{ Id='CK04'; File=$runhpp; Desc='accepted() is the validator only'; Find='{ return !stoppedAt.has_value() && validation.accepted(); }'; Replace='{ return validation.accepted(); }' },
 @{ Id='CK05'; File=$runcpp; Desc='the refusal of a link is not recorded'; Find='    run.stoppedAt = stage;\n    run.failure = std::move(error);'; Replace='    (void)run;\n    (void)stage;\n    (void)error;' },
 @{ Id='CK06'; File=$runcpp; Desc='a transient analysis is run through the steady links'; Find='    if (definition.mode == ThermalAnalysisMode::Steady) {'; Replace='    if (definition.mode == ThermalAnalysisMode::Steady || definition.mode == ThermalAnalysisMode::Transient) {' },
 # ---- the finite-checking writer
 @{ Id='CO01'; File=$output; Desc='a non-finite number is written through to the payload'; Find='        if (!std::isfinite(value)) {'; Replace='        if (false && !std::isfinite(value)) {' },
 @{ Id='CO02'; File=$output; Desc='a non-finite number is replaced by null but not remembered'; Find='            sawNonFinite_ = true;\n            return JsonValue::null();'; Replace='            return JsonValue::null();' }
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
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-cli.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -Filter "ThermalCli_*,ThermalRun_*" -TimeoutSeconds 900 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $gate = if ($log -match 'GATE : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | gate {4} | {5} | {6:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $gate, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
if (-not $env:PER_DRYRUN) { Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8 }
