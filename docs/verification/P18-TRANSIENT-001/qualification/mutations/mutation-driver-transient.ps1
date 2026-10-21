$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TT = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalTransient.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[transient]"

# ---- the time grid
M 'G01' 'grid tolerance zero (a floating-point integer ratio leaves a sliver step)' $TT $F 'std::abs(ratio - nearest) <= kTimeGridTolerance * nearest' 'std::abs(ratio - nearest) <= 0.0'
M 'G02' 'grid tolerance 1e-3 (a real remainder is swallowed)' $TT $F 'std::abs(ratio - nearest) <= kTimeGridTolerance * nearest' 'std::abs(ratio - nearest) <= 1.0e-3 * nearest'
M 'G03' 'times accumulated by repeated addition' $TT $F '    return static_cast<double>(index) * nominal_;
}' '    double t = 0.0;
    for (std::size_t i = 0; i < index; ++i) {
        t += nominal_;
    }
    return t;
}'
M 'G04' 'shortened last step uses the nominal step' $TT $F 'return remainder_;' 'return nominal_;'
M 'G05' 'final time computed instead of being t_end exactly' $TT $F '    if (index >= steps_) {
        return end_;
    }
    return static_cast<double>(index) * nominal_;' '    if (index > steps_) {
        return end_;
    }
    return static_cast<double>(index) * nominal_;'
M 'G06' 'floor replaced by ceil for the number of full steps' $TT $F 'const double full = std::floor(ratio);' 'const double full = std::ceil(ratio);'
M 'G07' 'resource guard on the step count removed' $TT $F 'if (!(ratio <= static_cast<double>(kMaxTimeSteps))) {' 'if (false) {'
M 'G08' 'a zero end time is not special-cased' $TT $F '    if (endTime == 0.0) {
        return grid; // one time, no step
    }
' ''
M 'G09' 'integrated span reports the end time' $TT $F '    return static_cast<double>(steps_) * nominal_;
}' '    return end_;
}'
# ---- the effective matrix
M 'E01' 'C multiplied by dt instead of divided' $TT $F 'values.push_back(cValues[c] / timeStep + kValues[k]);' 'values.push_back(cValues[c] * timeStep + kValues[k]);'
M 'E02' 'capacity-only entries not divided by dt' $TT $F 'values.push_back(cValues[c] / timeStep);' 'values.push_back(cValues[c]);'
M 'E03' 'conductance-only entries dropped' $TT $F '                values.push_back(kValues[k]);' '                values.push_back(0.0);'
M 'E04' 'symmetry gate of the effective matrix removed' $TT $F 'kEffectiveSymmetryTolerance);' 'std::nullopt);'
M 'E05' 'lumped capacity (off-diagonal C dropped) in the effective matrix' $TT $F 'values.push_back(cValues[c] / timeStep + kValues[k]);' 'values.push_back((cInner[c] == row ? cValues[c] : 0.0) / timeStep + kValues[k]);'
M 'E06' 'epsilon added to the effective diagonal' $TT $F 'values.push_back(cValues[c] / timeStep + kValues[k]);' 'values.push_back(cValues[c] / timeStep + kValues[k] + (kInner[k] == row ? 1.0e-9 : 0.0));'
# ---- the history term
M 'H01' 'history sign flipped' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i] - (*product)[i] / timeStep;'
M 'H02' 'history term omitted' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i];'
M 'H03' 'history multiplied by dt instead of divided' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i] + (*product)[i] * timeStep;'
M 'H04' 'history not divided by dt' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = heat.values()[i] + (*product)[i];'
M 'H05' 'heat input omitted from the right-hand side' $TT $F 'out[i] = heat.values()[i] + (*product)[i] / timeStep;' 'out[i] = (*product)[i] / timeStep;'
# ---- the run
M 'A01' 'prescribed does not dominate the initial temperature' $TT $F '        slot = constraint.temperature;' '        ;'
M 'A02' 'initial overrides not counted' $TT $F '            ++out.initialOverrides;' '            ;'
M 'A03' 'the steady anchoring analysis leaks into the transient solve' $TT $F '        TransientStepRecord record;
        record.index = k + 1;' '        if (analyseAnchoring(*reduced).unanchored > 0) {
            return fail(failure(TransientProblem::SingularSystem, ErrorCode::FailedPrecondition, "unanchored"));
        }
        TransientStepRecord record;
        record.index = k + 1;'
M 'A04' 'the state does not advance from step to step' $TT $F '        previous = std::move(*next);' '        (void)next;'
M 'A05' 'the nominal step is used for every step, including the shortened one' $TT $F 'const double timeStep = grid.stepAt(k);' 'const double timeStep = grid.nominalStep();'
M 'A06' 'the per-step acceptance is skipped inside the run' $TT $F 'validateTransientStep(conductance, capacity, heat, equations, constraints, previous, *next, timeStep, settings);' 'TransientStepResidual{};'
M 'A07' 'the effective matrix is not rebuilt when dt changes' $TT $F 'if (!effective.has_value() || effectiveStep != timeStep) {' 'if (!effective.has_value()) {'
M 'A08' 'the history-size guard removed' $TT $F 'if (n > 0 && sampleCount > kMaxStoredValues / n) {' 'if (false) {'
M 'A09' 'EveryStep keeps no state after the initial one' $TT $F '        previous = std::move(*next);
        if (output == TransientOutputPolicy::EveryStep) {' '        previous = std::move(*next);
        if (false) {'
M 'A10' 'the final state is stored twice for EveryStep' $TT $F '    if (output == TransientOutputPolicy::FinalOnly) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(stepCount)));' '    if (true) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(stepCount)));'
M 'A11' 'EveryStep omits the initial state' $TT $F '    if (output == TransientOutputPolicy::EveryStep) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));' '    if (false) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));'
M 'A12' 'kernel solves not counted' $TT $F '            ++out.kernelSolves;' '            ;'
M 'A13' 'singular reported as a factorisation failure' $TT $F '        if (refused.exactlyZeroPivot) {
            return failure(TransientProblem::SingularSystem,' '        if (refused.exactlyZeroPivot) {
            return failure(TransientProblem::FactorizationFailure,'
M 'A14' 'the pivot floor is not the user setting' $TT $F '.pivotFloor = settings.pivotFloor}' '.pivotFloor = 1.0e-12}'
M 'A15' 'the residual tolerance is not the user setting' $TT $F '.relativeResidualTolerance = settings.relativeResidualTolerance,' '.relativeResidualTolerance = 1.0e-9,'
# ---- the step acceptance
M 'V01' 'constraint postcondition checked on the previous state instead of the candidate' $TT $F 'constraintsHeld(constraints, next); broken.has_value()' 'constraintsHeld(constraints, previous); broken.has_value()'
M 'V02' 'candidate finiteness check removed' $TT $F 'if (!std::isfinite(next[i].si())) {' 'if (n > 0xFFFFFFFFFFFFULL) {'
M 'V03' 'previous-state finiteness check removed' $TT $F 'if (!std::isfinite(previous[i].si())) {' 'if (n > 0xFFFFFFFFFFFFULL) {'
M 'V04' 'physical residual threshold not applied' $TT $F 'if (!(*physical <= settings.relativeResidualTolerance)) {' 'if (false) {'
M 'V05' 'reduced residual not recorded' $TT $F '        out.reduced = norms->normalized;' '        out.reduced = 0.0;'
M 'V06' 'physical residual not recorded' $TT $F '    out.physical = *physical;' '    out.physical = 0.0;'
M 'R01' 'physical residual: heat input added instead of subtracted' $TT $F 'const double r = (*cd)[row] / timeStep + (*kt)[row] - q;' 'const double r = (*cd)[row] / timeStep + (*kt)[row] + q;'
M 'R02' 'physical residual: the capacity term dropped' $TT $F 'const double r = (*cd)[row] / timeStep + (*kt)[row] - q;' 'const double r = (*kt)[row] - q;'
M 'R03' 'physical residual: the capacity term multiplied by dt' $TT $F 'const double r = (*cd)[row] / timeStep + (*kt)[row] - q;' 'const double r = (*cd)[row] * timeStep + (*kt)[row] - q;'
M 'R04' 'physical residual: every row counted, prescribed boundary heat included' $TT $F '    for (const ThermalDofIndex dof : freeDofs) {
        const auto row = static_cast<std::size_t>(dof.value());' '    (void)freeDofs;
    for (std::size_t row = 0; row < nextSi.size(); ++row) {'
M 'R05' 'physical residual: denominator without the capacity term' $TT $F 'const double denominator = infinityNorm(capacity) * euclidean(change) / timeStep +' 'const double denominator = 0.0 * infinityNorm(capacity) * euclidean(change) / timeStep +'
M 'R06' 'physical residual: denominator without the conductance term' $TT $F 'infinityNorm(conductance) * euclidean(nextSi) +' '0.0 * infinityNorm(conductance) * euclidean(nextSi) +'
M 'R07' 'physical residual: per-row finiteness check removed' $TT $F '        if (!std::isfinite(r)) {' '        if (false) {'
M 'R08' 'physical residual: norm finiteness check removed' $TT $F 'if (!std::isfinite(numerator) || !std::isfinite(denominator)) {' 'if (false) {'
# ---- provenance
M 'P01' 'initial condition dropped from the solution source' $TT $F '                                                            .initial = initial,
                                                            .transient = transient,
                                                            .settings = settings},' '                                                            .initial = UniformInitialTemperature{},
                                                            .transient = transient,
                                                            .settings = settings},'
M 'P02' 'transient settings dropped from the solution source' $TT $F '                                                            .transient = transient,
                                                            .settings = settings},' '                                                            .transient = TransientSettings{},
                                                            .settings = settings},'
M 'P03' 'solver settings dropped from the solution source' $TT $F '                                                            .settings = settings},
                                    std::move(*run)};' '                                                            .settings = ThermalSolverSettings{}},
                                    std::move(*run)};'
M 'P04' 'prescribed conditions dropped from the solution source' $TT $F '.temperatures = std::move(temperatureConditions),' '.temperatures = {},'
M 'P05' 'prescribed temperatures ignored by the staleness comparison' $TT $F 'if (!(solved.temperatures == current.temperatures)) {' 'if (false) {'
M 'P06' 'initial condition ignored by the staleness comparison' $TT $F 'if (!(solved.initial == current.initial)) {' 'if (false) {'
M 'P07' 'time step ignored by the staleness comparison' $TT $F 'if (!(solved.transient.timeStep == current.transient.timeStep)) {' 'if (false) {'
M 'P08' 'end time ignored by the staleness comparison' $TT $F 'if (!(solved.transient.endTime == current.transient.endTime)) {' 'if (false) {'
M 'P09' 'output policy ignored by the staleness comparison' $TT $F 'if (!(solved.transient.output == current.transient.output)) {' 'if (false) {'
M 'P10' 'solver settings ignored by the staleness comparison' $TT $F 'if (!(solved.settings == current.settings)) {' 'if (false) {'
M 'P11' 'current source computed for steady mode' $TT $F 'currentThermalSystemSource(document, model, ThermalAnalysisMode::Transient, loads, conditions);' 'currentThermalSystemSource(document, model, ThermalAnalysisMode::Steady, loads, conditions);'
M 'P12' 'the assembled system is checked for steady mode' $TT $F 'requireCurrent(system, document, model, ThermalAnalysisMode::Transient, loads, conditions);' 'requireCurrent(system, document, model, ThermalAnalysisMode::Steady, loads, conditions);'
M 'P13' 'stale assembled system accepted' $TT $F '        !current.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::StaleSystem' '        false && !current.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::StaleSystem'
M 'P14' 'stale prepared boundary accepted' $TT $F '        !stale.empty()) {
        std::string names;' '        false && !stale.empty()) {
        std::string names;'
M 'P15' 'numbering of another mesh accepted' $TT $F 'if (!system.describes(map)) {' 'if (false) {'
M 'P16' 'every condition recorded as a prescribed temperature' $TT $F 'if (condition.prescribedTemperature() != nullptr) {' 'if (true) {'
M 'P17' 'prepared boundary of another numbering accepted' $TT $F 'if (!preparedBoundary.describes(map)) {' 'if (false) {'
# ---- settings
M 'S01' 'end time shorter than one step accepted' $TT $F '|| !(end > 0.0) || end < dt) {' '|| !(end > 0.0)) {'
M 'S02' 'time step not checked before the system' $TT $F '    const double end = transient.endTime.si();
    if (!std::isfinite(dt) || !(dt > 0.0)) {' '    const double end = transient.endTime.si();
    if (!std::isfinite(dt)) {'
M 'S03' 'initial temperature not checked before the system' $TT $F '    if (Result<void> checked = validate(initial); !checked.has_value()) {' '    if (Result<void> checked = Result<void>{}; !checked.has_value()) {'
M 'S04' 'output policy not checked before the system' $TT $F '    if (!knownPolicy(transient.output)) {
        return failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                       "the transient output policy is not one this build knows");
    }
    if (Result<void> checked = validate(initial)' '    if (false) {
        return failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                       "the transient output policy is not one this build knows");
    }
    if (Result<void> checked = validate(initial)'
M 'S05' 'a steady-mode definition is taken for a transient one' $TT $F 'definition.mode != ThermalAnalysisMode::Transient ||' 'false ||'
"" | Out-File "$scr\mutation-transient-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate-t.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-transient-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-transient-progress.txt" -Append
