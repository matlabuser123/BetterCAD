# P18-PERSIST-001 mutation campaign driver. Each probe edits the named production file, rebuilds bettercad_tests (debug-ext), runs the
# ThermalPersistence_* cases (and the P17 and P16 persistence suites that share the dispatch) and restores the file (mutate-t.ps1).
# The verdict is read from the TEST OUTPUT. A literal backslash-n in Find/Replace stands for a newline (the sources are LF).
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$tj = "$root\src\io\json\ThermalAnalysisJson.cpp"
$dj = "$root\src\io\json\DocumentJson.cpp"
$progress = "$s\persist-mutation-progress.txt"
$only = $args
$probes = @(
 # ---- the writer: a field dropped, rescaled or reshaped
 @{ Id='PER01'; File=$tj; Desc='the pivot floor is not written'; Find='json["pivot_floor"] = solver.pivotFloor;'; Replace='(void)solver.pivotFloor;' },
 @{ Id='PER02'; File=$tj; Desc='the residual tolerance is not written'; Find='json["relative_residual_tolerance"] = solver.relativeResidualTolerance;'; Replace='(void)solver.relativeResidualTolerance;' },
 @{ Id='PER03'; File=$tj; Desc='a surface flux is written as its absolute value'; Find='json["flux"] = surface->flux.si();'; Replace='json["flux"] = surface->flux.si() < 0.0 ? -surface->flux.si() : surface->flux.si();' },
 @{ Id='PER04'; File=$tj; Desc='a generation is written as its absolute value'; Find='json["generation"] = source->generation.si();'; Replace='json["generation"] = source->generation.si() < 0.0 ? -source->generation.si() : source->generation.si();' },
 @{ Id='PER05'; File=$tj; Desc='a held temperature is written in Celsius'; Find='json["temperature"] = held->value.si();'; Replace='json["temperature"] = held->value.si() - 273.15;' },
 @{ Id='PER06'; File=$tj; Desc='a convection coefficient is written in kW/(m^2 K)'; Find='json["coefficient"] = convection->coefficient.si();'; Replace='json["coefficient"] = convection->coefficient.si() / 1000.0;' },
 @{ Id='PER07'; File=$tj; Desc='the ambient is written from the coefficient'; Find='json["ambient"] = convection->ambient.si();'; Replace='json["ambient"] = convection->coefficient.si();' },
 @{ Id='PER08'; File=$tj; Desc='the time step is written in milliseconds'; Find='json["time_step"] = transient.timeStep.si();'; Replace='json["time_step"] = transient.timeStep.si() * 1000.0;' },
 @{ Id='PER09'; File=$tj; Desc='the end time is written in minutes'; Find='json["end_time"] = transient.endTime.si();'; Replace='json["end_time"] = transient.endTime.si() / 60.0;' },
 @{ Id='PER10'; File=$tj; Desc='the initial temperature is not written'; Find='json["initial_temperature"] = d.initialCondition->value.si();'; Replace='(void)d;' },
 @{ Id='PER11'; File=$tj; Desc='the loads are written in reverse order'; Find='    for (const thermal::ThermalLoad& load : loads) {\n        array.push_back(loadToJson(load));\n    }'; Replace='    for (std::size_t i = loads.size(); i > 0U; --i) {\n        array.push_back(loadToJson(loads[i - 1U]));\n    }' },
 @{ Id='PER12'; File=$tj; Desc='the conditions are written in reverse order'; Find='    for (const thermal::ThermalBoundaryCondition& condition : conditions) {\n        array.push_back(conditionToJson(condition));\n    }'; Replace='    for (std::size_t i = conditions.size(); i > 0U; --i) {\n        array.push_back(conditionToJson(conditions[i - 1U]));\n    }' },
 @{ Id='PER13'; File=$tj; Desc='a load face is rebound to the end cap on the way out'; Find='json["face"] = faceNameToJson(surface->face);'; Replace='json["face"] = faceNameToJson(FaceName{surface->face.feature, FaceSelector{.role = FaceRole::EndCap}});' },
 @{ Id='PER14'; File=$tj; Desc='the writer adds the analysis revision to the file'; Find='json["mesh"] = d.mesh.value();'; Replace='json["mesh"] = d.mesh.value();\n    json["revision"] = analysis.revision();' },
 @{ Id='PER15'; File=$tj; Desc='empty load and condition arrays are always written'; Find='    if (!d.loads.empty()) {'; Replace='    if (true) {' },
 @{ Id='PER16'; File=$tj; Desc='the load kind keys are swapped'; Find='{thermal::ThermalLoadKind::SurfaceHeatFlux, "surface_heat_flux"},\n    {thermal::ThermalLoadKind::VolumetricHeatSource, "volumetric_heat_source"},'; Replace='{thermal::ThermalLoadKind::SurfaceHeatFlux, "volumetric_heat_source"},\n    {thermal::ThermalLoadKind::VolumetricHeatSource, "surface_heat_flux"},' },
 @{ Id='PER17'; File=$tj; Desc='the mode keys are swapped'; Find='{thermal::ThermalAnalysisMode::Steady, "steady"},\n    {thermal::ThermalAnalysisMode::Transient, "transient"},'; Replace='{thermal::ThermalAnalysisMode::Steady, "transient"},\n    {thermal::ThermalAnalysisMode::Transient, "steady"},' },
 @{ Id='PER18'; File=$tj; Desc='the output policy keys are swapped'; Find='{thermal::TransientOutputPolicy::FinalOnly, "final_only"},\n    {thermal::TransientOutputPolicy::EveryStep, "every_step"},'; Replace='{thermal::TransientOutputPolicy::FinalOnly, "every_step"},\n    {thermal::TransientOutputPolicy::EveryStep, "final_only"},' },
 # ---- the reader: a value altered, a rule dropped
 @{ Id='PER19'; File=$tj; Desc='a load id is regenerated as id + 1 on load'; Find='const ThermalLoadId loadId = ThermalLoadId::fromValue(*id);'; Replace='const ThermalLoadId loadId = ThermalLoadId::fromValue(*id + 1U);' },
 @{ Id='PER20'; File=$tj; Desc='a condition id is regenerated as id + 1 on load'; Find='const ThermalBoundaryConditionId conditionId = ThermalBoundaryConditionId::fromValue(*id);'; Replace='const ThermalBoundaryConditionId conditionId = ThermalBoundaryConditionId::fromValue(*id + 1U);' },
 @{ Id='PER21'; File=$tj; Desc='a duplicate load id is accepted (last write wins in the vector)'; Find='if (seen.id() == load->id()) {'; Replace='if (seen.id() == load->id() && false) {' },
 @{ Id='PER22'; File=$tj; Desc='a duplicate condition id is accepted'; Find='if (seen.id() == condition->id()) {'; Replace='if (seen.id() == condition->id() && false) {' },
 @{ Id='PER23'; File=$tj; Desc='the core load validator is not called on read'; Find='if (const Result<void> valid = thermal::validate(*load); !valid) {'; Replace='if (const Result<void> valid = Result<void>{}; !valid) {' },
 @{ Id='PER24'; File=$tj; Desc='the core condition validator is not called on read'; Find='if (const Result<void> valid = thermal::validate(*condition); !valid) {'; Replace='if (const Result<void> valid = Result<void>{}; !valid) {' },
 @{ Id='PER25'; File=$tj; Desc='the core solver-settings validator is not called on read'; Find='if (const Result<void> valid = thermal::validate(solver); !valid) {'; Replace='if (const Result<void> valid = Result<void>{}; !valid) {' },
 @{ Id='PER26'; File=$tj; Desc='the core transient-settings validator is not called on read'; Find='if (const Result<void> valid = thermal::validate(transient); !valid) {'; Replace='if (const Result<void> valid = Result<void>{}; !valid) {' },
 @{ Id='PER27'; File=$tj; Desc='the keys a kind must not carry are no longer refused'; Find='if (value.contains(std::string{key})) {'; Replace='if (false && value.contains(std::string{key})) {' },
 @{ Id='PER28'; File=$tj; Desc='an unknown enumerator is read as the first one'; Find='    return parseError(childPath(path, key), std::format("unknown {} ''{}''", what, *text));'; Replace='    (void)what;\n    return table[0].first;' },
 @{ Id='PER29'; File=$tj; Desc='the initial temperature is read as Celsius'; Find='const thermal::UniformInitialTemperature condition{Temperature::fromSi(*initial)};'; Replace='const thermal::UniformInitialTemperature condition{Temperature::fromSi(*initial + 273.15)};' },
 @{ Id='PER30'; File=$tj; Desc='the time step is read as milliseconds'; Find='.timeStep = Time::fromSi(*step),'; Replace='.timeStep = Time::fromSi(*step / 1000.0),' },
 @{ Id='PER31'; File=$tj; Desc='a surface flux sign is flipped on read'; Find='.flux = HeatFlux::fromSi(*flux)}};'; Replace='.flux = HeatFlux::fromSi(-*flux)}};' },
 @{ Id='PER32'; File=$tj; Desc='a generation is read as its absolute value'; Find='.generation = VolumetricHeatSource::fromSi(*generation)}};'; Replace='.generation = VolumetricHeatSource::fromSi(*generation < 0.0 ? -*generation : *generation)}};' },
 @{ Id='PER33'; File=$tj; Desc='the two solver tolerances are swapped on read'; Find='const thermal::ThermalSolverSettings solver{.relativeResidualTolerance = *tolerance,\n                                                .pivotFloor = *pivot};'; Replace='const thermal::ThermalSolverSettings solver{.relativeResidualTolerance = *pivot,\n                                                .pivotFloor = *tolerance};' },
 @{ Id='PER34'; File=$tj; Desc='the output policy is always read as final-only'; Find='.output = *output};'; Replace='.output = thermal::TransientOutputPolicy::FinalOnly};' },
 @{ Id='PER35'; File=$tj; Desc='the strict key list is loosened so generated keys are accepted and ignored'; Find='"initial_temperature", "solver", "transient"});'; Replace='"initial_temperature", "solver", "transient", "temperature", "validated"});' },
 @{ Id='PER36'; File=$tj; Desc='a load face is rebound on read (the nearest-face repair)'; Find='        auto face = readFace(**faceField, childPath(path, "face"));\n        if (!face) {\n            return std::unexpected(face.error());\n        }\n        load = thermal::ThermalLoad{'; Replace='        Result<FaceName> face = FaceName{ObjectId::fromValue(1U), FaceSelector{.role = FaceRole::EndCap}};\n        load = thermal::ThermalLoad{' },
 @{ Id='PER38'; File=$tj; Desc='the invalid feature handle is accepted as a target'; Find='if (!face->feature.isValid()) {'; Replace='if (false) {' },
 @{ Id='PER39'; File=$tj; Desc='the writer no longer refuses an identifier above the file cap'; Find='if (value > kMaxFileId) {'; Replace='if (false) {' },
 @{ Id='PER40'; File=$tj; Desc='the writer emits mode before mesh (key order)'; Find='json["mesh"] = d.mesh.value();\n    json["mode"] = enumName(kModes, d.mode);'; Replace='json["mode"] = enumName(kModes, d.mode);\n    json["mesh"] = d.mesh.value();' },
 @{ Id='PER41'; File=$tj; Desc='the writer emits the solver section before the loads (key order)'; Find='    if (!d.loads.empty()) {\n        json["loads"] = loadsToJson(d.loads);\n    }'; Replace='    json["solver"] = solverToJson(d.solver);\n    if (!d.loads.empty()) {\n        json["loads"] = loadsToJson(d.loads);\n    }' }, @{ Id='PER37'; File=$dj; Desc='the reader no longer dispatches thermal-analysis'; Find='if (*type == "thermal-analysis") {'; Replace='if (*type == "thermal-analysis-x") {' }
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
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-t.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -Filter $(if ($env:PER_FILTER) { $env:PER_FILTER } else { "ThermalPersistence_*,StructuralPersistence_*,MeshingPersistence_*" }) -TimeoutSeconds 900 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | {4} | {5:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
if (-not $env:PER_DRYRUN) { Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8 }
