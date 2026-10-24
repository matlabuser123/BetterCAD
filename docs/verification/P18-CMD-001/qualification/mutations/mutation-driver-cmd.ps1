# P18-CMD-001 mutation campaign driver. Each probe edits the named production file, rebuilds bettercad_tests (debug-ext), runs the
# ThermalCommand_* cases and restores the file (mutate-t.ps1). The verdict is read from the TEST OUTPUT.
# A literal backslash-n in Find/Replace stands for a newline (the sources are LF).
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cc = "$root\src\thermal\ThermalCommands.cpp"
$progress = "$s\cmd-mutation-progress.txt"
$only = $args
$probes = @(
 # ---- order, identity and the shape of an add / edit / remove
 @{ Id='CMD01'; File=$cc; Desc='an added load is inserted at the front, not appended'; Find='after.loads.push_back(load_);'; Replace='after.loads.insert(after.loads.begin(), load_);' },
 @{ Id='CMD02'; File=$cc; Desc='an added condition is inserted at the front, not appended'; Find='after.boundaryConditions.push_back(condition_);'; Replace='after.boundaryConditions.insert(after.boundaryConditions.begin(), condition_);' },
 @{ Id='CMD03'; File=$cc; Desc='a load edit moves the record to the end instead of replacing it in place'; Find='    *at = replacement_;\n    return after;\n}\n\nRemoveThermalLoadCommand'; Replace='    after.loads.erase(at);\n    after.loads.push_back(replacement_);\n    return after;\n}\n\nRemoveThermalLoadCommand' },
 @{ Id='CMD04'; File=$cc; Desc='a condition edit moves the record to the end instead of replacing it in place'; Find='    *at = replacement_;\n    return after;\n}\n\nRemoveThermalBoundaryConditionCommand::RemoveThermalBoundaryConditionCommand('; Replace='    after.boundaryConditions.erase(at);\n    after.boundaryConditions.push_back(replacement_);\n    return after;\n}\n\nRemoveThermalBoundaryConditionCommand::RemoveThermalBoundaryConditionCommand(' },
 @{ Id='CMD05'; File=$cc; Desc='an edit of a load that does not exist creates it'; Find='    if (at == after.loads.end()) {\n        // REFUSED, NOT CREATED'; Replace='    if (at == after.loads.end()) {\n        after.loads.push_back(replacement_);\n        return after;\n    }\n    if (false) {\n        // REFUSED, NOT CREATED' },
 @{ Id='CMD06'; File=$cc; Desc='a duplicate load id is accepted'; Find='if (findLoad(after.loads, load_.id()) != after.loads.end()) {'; Replace='if (false) {' },
 @{ Id='CMD07'; File=$cc; Desc='a duplicate condition id is accepted'; Find='if (findCondition(after.boundaryConditions, condition_.id()) != after.boundaryConditions.end()) {'; Replace='if (false) {' },
 @{ Id='CMD08'; File=$cc; Desc='removing a load that does not exist succeeds silently'; Find='    const auto at = findLoad(after.loads, load_);\n    if (at == after.loads.end()) {'; Replace='    const auto at = findLoad(after.loads, load_);\n    if (at == after.loads.end()) {\n        return after;\n    }\n    if (false) {' },
 @{ Id='CMD09'; File=$cc; Desc='removing a condition that does not exist succeeds silently'; Find='    const auto at = findCondition(after.boundaryConditions, condition_);\n    if (at == after.boundaryConditions.end()) {'; Replace='    const auto at = findCondition(after.boundaryConditions, condition_);\n    if (at == after.boundaryConditions.end()) {\n        return after;\n    }\n    if (false) {' },
 @{ Id='CMD10'; File=$cc; Desc='removing a load removes the last load'; Find='after.loads.erase(at);'; Replace='after.loads.pop_back();' },
 @{ Id='CMD11'; File=$cc; Desc='removing a condition removes the first condition'; Find='after.boundaryConditions.erase(at);\n    return after;\n}\n\n// ---------------------------------------------------------------------------\n// Initial temperature'; Replace='after.boundaryConditions.erase(after.boundaryConditions.begin());\n    return after;\n}\n\n// ---------------------------------------------------------------------------\n// Initial temperature' },
 @{ Id='CMD12'; File=$cc; Desc='a load is addressed by its raw id as a position'; Find='const auto at = findLoad(after.loads, load_);'; Replace='const auto at = (load_.value() < after.loads.size()) ? after.loads.begin() + static_cast<std::ptrdiff_t>(load_.value()) : after.loads.end();' },
 # ---- the recorded payload and undo / redo
 @{ Id='CMD13'; File=$cc; Desc='the recorded before-state is the after-state'; Find='before_ = before;\n    after_ = *after;'; Replace='before_ = *after;\n    after_ = *after;' },
 @{ Id='CMD14'; File=$cc; Desc='a refused apply leaves the command staged as executed'; Find='        before_.reset();\n        after_.reset();'; Replace='        (void)0;' },
 @{ Id='CMD15'; File=$cc; Desc='a no-op records the requested definition instead of what the document holds'; Find='        after_ = before;'; Replace='        (void)0;' },
 @{ Id='CMD16'; File=$cc; Desc='a no-op is reported as a change'; Find='changed_ = *changed;'; Replace='changed_ = true;' },
 @{ Id='CMD17'; File=$cc; Desc='undo no longer checks that the analysis is as the command left it'; Find='if (!(object->definition() == *after_)) {'; Replace='if (false) {' },
 @{ Id='CMD18'; File=$cc; Desc='redo no longer checks that the analysis is as the command found it'; Find='if (!(object->definition() == *before_)) {'; Replace='if (false) {' },
 @{ Id='CMD19'; File=$cc; Desc='undo assigns the after-state (undo does nothing)'; Find='Result<bool> restored = apply(document, analysis_, *before_);'; Replace='Result<bool> restored = apply(document, analysis_, *after_);' },
 @{ Id='CMD20'; File=$cc; Desc='redo assigns the before-state (redo does nothing)'; Find='Result<bool> reapplied = apply(document, analysis_, *after_);'; Replace='Result<bool> reapplied = apply(document, analysis_, *before_);' },
 @{ Id='CMD21'; File=$cc; Desc='a refused apply is reported as success (the command is recorded)'; Find='        before_.reset();\n        after_.reset();\n        return std::unexpected(changed.error());'; Replace='        before_.reset();\n        after_.reset();\n        return Result<void>{};' },
 @{ Id='CMD22'; File=$cc; Desc='a redo of a create makes a NEW analysis with a new id'; Find='    return add_->redo(document);'; Replace='    add_.reset();\n    return execute(document);' },
 @{ Id='CMD23'; File=$cc; Desc='an undo of a create leaves the analysis in the document'; Find='    return add_->undo(document);'; Replace='    (void)document;\n    return Result<void>{};' },
 @{ Id='CMD24'; File=$cc; Desc='a create silently drops the loads of its definition'; Find='auto analysis = ThermalAnalysis::create(name_, definition_);'; Replace='ThermalAnalysisDefinition dropped = definition_;\n    dropped.loads.clear();\n    auto analysis = ThermalAnalysis::create(name_, dropped);' },
 # ---- the settings commands
 @{ Id='CMD25'; File=$cc; Desc='a solver-settings command lands only the pivot floor'; Find='after.solver = settings_;'; Replace='after.solver.pivotFloor = settings_.pivotFloor;' },
 @{ Id='CMD26'; File=$cc; Desc='a transient-settings command keeps the old output policy'; Find='after.transient = settings_;'; Replace='after.transient = settings_;\n    if (before.transient.has_value()) {\n        after.transient->output = before.transient->output;\n    }' },
 @{ Id='CMD27'; File=$cc; Desc='an initial temperature is shifted by half a kelvin on its way in'; Find='after.initialCondition = initial_;'; Replace='UniformInitialTemperature shifted = initial_;\n    shifted.value = Temperature::fromSi(shifted.value.si() + 0.5);\n    after.initialCondition = shifted;' },
 @{ Id='CMD28'; File=$cc; Desc='a steady analysis accepts an initial temperature (mode not checked)'; Find='    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "initial temperature");\n    }'; Replace='' },
 @{ Id='CMD29'; File=$cc; Desc='a steady analysis accepts transient settings (mode not checked)'; Find='    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "time step or end time");\n    }'; Replace='' },
 @{ Id='CMD30'; File=$cc; Desc='the initial-temperature value is checked before the mode'; Find='    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "initial temperature");\n    }\n    // P18-DATA''s own validator: finite and not below 0 K. No Celsius, no rounding.\n    if (const Result<void> checked = validate(initial_); !checked.has_value()) {\n        return std::unexpected(checked.error());\n    }'; Replace='    if (const Result<void> checked = validate(initial_); !checked.has_value()) {\n        return std::unexpected(checked.error());\n    }\n    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "initial temperature");\n    }' },
 @{ Id='CMD31'; File=$cc; Desc='the transient-settings value is checked before the mode'; Find='    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "time step or end time");\n    }\n    // P18-DATA''s own validator: 0 < step <= end, both finite. A refused value is never corrected, and the three fields\n    // land together or none does.\n    if (const Result<void> checked = validate(settings_); !checked.has_value()) {\n        return std::unexpected(checked.error());\n    }'; Replace='    if (const Result<void> checked = validate(settings_); !checked.has_value()) {\n        return std::unexpected(checked.error());\n    }\n    if (before.mode != ThermalAnalysisMode::Transient) {\n        return steadyHasNo(analysis_, "time step or end time");\n    }' },
 # ---- the geometry-reference rule (a target must name a feature)
 @{ Id='CMD32'; File=$cc; Desc='the named-feature rule never fires'; Find='if (!face.feature.isValid()) {'; Replace='if (!face.feature.isValid() && false) {' },
 @{ Id='CMD33'; File=$cc; Desc='add-load no longer requires a named feature'; Find='if (const Result<void> named = requireNamedTarget(load_); !named.has_value()) {'; Replace='if (const Result<void> named = Result<void>{}; !named.has_value()) {' },
 @{ Id='CMD34'; File=$cc; Desc='edit-load no longer requires a named feature'; Find='    if (const Result<void> named = requireNamedTarget(replacement_); !named.has_value()) {\n        return std::unexpected(named.error());\n    }\n    ThermalAnalysisDefinition after = before;\n    const auto at = findLoad('; Replace='    ThermalAnalysisDefinition after = before;\n    const auto at = findLoad(' },
 @{ Id='CMD35'; File=$cc; Desc='add-condition no longer requires a named feature'; Find='if (const Result<void> named = requireNamedTarget(condition_); !named.has_value()) {'; Replace='if (const Result<void> named = Result<void>{}; !named.has_value()) {' },
 @{ Id='CMD36'; File=$cc; Desc='edit-condition no longer requires a named feature'; Find='    if (const Result<void> named = requireNamedTarget(replacement_); !named.has_value()) {\n        return std::unexpected(named.error());\n    }\n    ThermalAnalysisDefinition after = before;\n    const auto at = findCondition('; Replace='    ThermalAnalysisDefinition after = before;\n    const auto at = findCondition(' },
 @{ Id='CMD37'; File=$cc; Desc='a create no longer requires its loads to name a feature'; Find='if (const Result<void> named = requireNamedTarget(load); !named.has_value()) {'; Replace='if (const Result<void> named = (static_cast<void>(load), Result<void>{}); !named.has_value()) {' },
 @{ Id='CMD38'; File=$cc; Desc='a create no longer requires its conditions to name a feature'; Find='if (const Result<void> named = requireNamedTarget(condition); !named.has_value()) {'; Replace='if (const Result<void> named = (static_cast<void>(condition), Result<void>{}); !named.has_value()) {' },
 @{ Id='CMD39'; File=$cc; Desc='the command layer drops the record validator of an added load (defence in depth)'; Find='if (const Result<void> checked = validate(load_); !checked.has_value()) {'; Replace='if (const Result<void> checked = Result<void>{}; !checked.has_value()) {' },
 # ---- diagnostics and descriptions
 @{ Id='CMD40'; File=$cc; Desc='a missing analysis is reported as an invalid argument'; Find='ErrorCode::NotFound, std::format("there is no thermal analysis {}", analysis.value())'; Replace='ErrorCode::InvalidArgument, std::format("there is no thermal analysis {}", analysis.value())' },
 @{ Id='CMD41'; File=$cc; Desc='a diverged undo is reported as not found'; Find='        ErrorCode::FailedPrecondition,\n        std::format("cannot {} the edit of thermal analysis {}'; Replace='        ErrorCode::NotFound,\n        std::format("cannot {} the edit of thermal analysis {}' },
 @{ Id='CMD42'; File=$cc; Desc='a steady-mode refusal is reported as an invalid argument'; Find='        ErrorCode::FailedPrecondition,\n        std::format("thermal analysis {} is steady'; Replace='        ErrorCode::InvalidArgument,\n        std::format("thermal analysis {} is steady' },
 @{ Id='CMD43'; File=$cc; Desc='the load description loses its id'; Find='"Add {} load {}", toString(load_.kind()), load_.id().value()'; Replace='"Add {} load {}", toString(load_.kind()), 0' },
 @{ Id='CMD44'; File=$cc; Desc='the initial-temperature description shows the wrong unit'; Find='"Set initial temperature ({} K)"'; Replace='"Set initial temperature ({} C)"' }
)
if ($only.Count -gt 0) { $probes = $probes | Where-Object { $only -contains $_.Id } }
foreach ($p in $probes) {
    $find = $p.Find.Replace('\n', "`n")
    $replace = $p.Replace.Replace('\n', "`n")
    if ($env:CMD_DRYRUN) {
        $t = [IO.File]::ReadAllText($p.File)
        $n = ([regex]::Matches($t, [regex]::Escape($find))).Count
        "{0} occurrences={1}  {2}" -f $p.Id, $n, $p.Desc
        continue
    }
    $before = [IO.File]::ReadAllText($p.File)
    $started = Get-Date
    [IO.File]::WriteAllText("$s\find-$($p.Id).txt", $find, (New-Object Text.UTF8Encoding $false))
    [IO.File]::WriteAllText("$s\replace-$($p.Id).txt", $replace, (New-Object Text.UTF8Encoding $false))
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-t.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -Filter "ThermalCommand_*" -TimeoutSeconds 600 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | {4} | {5:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8
