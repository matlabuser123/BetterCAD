$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TT = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalTransient.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[transient]"

# ---- the run
M 'TA01' 'prescribed does not dominate the initial temperature' $TT $F '        slot = constraint.temperature;' '        ;'
M 'TA02' 'initial overrides not counted' $TT $F '            ++out.initialOverrides;' '            ;'
M 'TA03' 'the steady anchoring analysis leaks into the transient solve' $TT $F '        TransientStepRecord record;
        record.index = k + 1;' '        if (analyseAnchoring(*reduced).unanchored > 0) {
            return fail(failure(TransientProblem::SingularSystem, ErrorCode::FailedPrecondition, "unanchored"));
        }
        TransientStepRecord record;
        record.index = k + 1;'
M 'TA04' 'the state does not advance from step to step' $TT $F '        previous = std::move(*next);' '        (void)next;'
M 'TA05' 'the nominal step is used for every step, including the shortened one' $TT $F 'const double timeStep = grid.stepAt(k);' 'const double timeStep = grid.nominalStep();'
M 'TA06' 'the per-step acceptance is skipped inside the run' $TT $F 'validateTransientStep(conductance, capacity, heat, equations, constraints, previous, *next, timeStep, settings);' 'TransientStepResidual{};'
M 'TA07' 'the effective matrix is not rebuilt when dt changes' $TT $F 'if (!effective.has_value() || effectiveStep != timeStep) {' 'if (!effective.has_value()) {'
M 'TA08' 'the history-size guard removed' $TT $F 'if (n > 0 && sampleCount > kMaxStoredValues / n) {' 'if (false) {'
M 'TA09' 'EveryStep keeps no state after the initial one' $TT $F '        previous = std::move(*next);
        if (output == TransientOutputPolicy::EveryStep) {' '        previous = std::move(*next);
        if (false) {'
M 'TA10' 'the final state is stored twice for EveryStep' $TT $F '    if (output == TransientOutputPolicy::FinalOnly) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(stepCount)));' '    if (true) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(stepCount)));'
M 'TA11' 'EveryStep omits the initial state' $TT $F '    if (output == TransientOutputPolicy::EveryStep) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));' '    if (false) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));'
M 'TA12' 'kernel solves not counted' $TT $F '            ++out.kernelSolves;' '            ;'
M 'TA13' 'singular reported as a factorisation failure' $TT $F '        if (refused.exactlyZeroPivot) {
            return failure(TransientProblem::SingularSystem,' '        if (refused.exactlyZeroPivot) {
            return failure(TransientProblem::FactorizationFailure,'
M 'TA14' 'the pivot floor is not the user setting' $TT $F '.pivotFloor = settings.pivotFloor}' '.pivotFloor = 1.0e-12}'
M 'TA15' 'the residual tolerance is not the user setting' $TT $F '.relativeResidualTolerance = settings.relativeResidualTolerance,' '.relativeResidualTolerance = 1.0e-9,'
M 'TV02' 'candidate finiteness check removed' $TT $F 'if (!std::isfinite(next[i].si())) {' 'if (n > 0xFFFFFFFFFFFFULL) {'
M 'TV03' 'previous-state finiteness check removed' $TT $F 'if (!std::isfinite(previous[i].si())) {' 'if (n > 0xFFFFFFFFFFFFULL) {'
M 'TV04' 'physical residual threshold not applied' $TT $F 'if (!(*physical <= settings.relativeResidualTolerance)) {' 'if (false) {'
M 'TS03' 'initial temperature not checked before the system' $TT $F '    if (Result<void> checked = validate(initial); !checked.has_value()) {' '    if (Result<void> checked = Result<void>{}; !checked.has_value()) {'
M 'TS04' 'output policy not checked before the system' $TT $F '    if (!knownPolicy(transient.output)) {
        return failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                       "the transient output policy is not one this build knows");
    }
    if (Result<void> checked = validate(initial)' '    if (false) {
        return failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                       "the transient output policy is not one this build knows");
    }
    if (Result<void> checked = validate(initial)'
M 'TS05' 'a steady-mode definition is taken for a transient one' $TT $F 'definition.mode != ThermalAnalysisMode::Transient ||' 'false ||'
# ---- reformulations of the four that did not compile in the first pass
M 'TH02' 'history term omitted (reformulated: no unused parameter)' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i] + 0.0 * (*product)[i] / timeStep;'
M 'TH04' 'history not divided by dt (reformulated)' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i] + (*product)[i] + 0.0 * timeStep;'
M 'TH05' 'heat input omitted from the right-hand side (reformulated)' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = 0.0 * heat.values()[i] + (*product)[i] / timeStep;'
M 'TP04' 'prescribed conditions dropped from the solution source (reformulated)' $TT $F '.temperatures = std::move(temperatureConditions),' '.temperatures = (temperatureConditions.clear(), std::vector<ThermalBoundaryCondition>{}),'
# ---- probes aimed at the tests added after the first pass
M 'TG10' 'integrated span of a shortened grid computed as N dt' $TT $F '        return static_cast<double>(steps_ - 1) * nominal_ + remainder_;' '        return static_cast<double>(steps_) * nominal_;'
M 'TA16' 'kernel-invoked flag not set' $TT $F '            record.kernelInvoked = true;' '            ;'
M 'TA17' 'pivot ratio not recorded' $TT $F '            record.pivotRatio = solved->pivotRatio;' '            ;'
M 'TA18' 'step index off by one' $TT $F '        record.index = k + 1;' '        record.index = k;'
M 'TA19' 'resolved initial state replaced by the raw one' $TT $F '    out.initial = previous;' '    out.initial = std::vector<Temperature>(initial.begin(), initial.end());'
M 'TA20' 'first EveryStep sample is the unresolved initial state' $TT $F '        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));
        out.samples.insert(out.samples.end(), previous.begin(), previous.end());' '        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));
        out.samples.insert(out.samples.end(), initial.begin(), initial.end());'
M 'TX02' 'sample offset ignores the unknown count' $TT $F 'subspan(index * data_.dofCount, data_.dofCount)' 'subspan(index, data_.dofCount)'
M 'TX03' 'final temperatures are the first sample' $TT $F 'temperaturesAt(data_.sampleTimes.size() - 1);' 'temperaturesAt(0);'
M 'TX04' 'worst reduced residual is the smallest' $TT $F 'worst = std::max(worst, record.reducedResidual);' 'worst = std::min(worst, record.reducedResidual);'
M 'TX05' 'worst physical residual is the smallest' $TT $F 'worst = std::max(worst, record.physicalResidual);' 'worst = std::min(worst, record.physicalResidual);'
M 'TX06' 'load edits reported as convection edits' $TT $F '    case SystemStaleReason::Loads:
        return TransientStaleReason::Loads;' '    case SystemStaleReason::Loads:
        return TransientStaleReason::Convections;'
M 'TX07' 'two diagnostic names collide' $TT $F 'return "stale_system";' 'return "stale_boundary";'
M 'TX08' 'current source drops the prescribed conditions' $TT $F '.temperatures = prescribedConditions(conditions),' '.temperatures = {},'
"" | Out-File "$scr\mutation-transient2-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate-t.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-transient2-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-transient2-progress.txt" -Append
