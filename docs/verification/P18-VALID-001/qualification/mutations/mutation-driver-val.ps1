# P18-VALID-001 mutation campaign driver. Each probe edits the named production file, rebuilds bettercad_tests (debug-ext), runs the
# [validation] cases and restores the file (mutate-t.ps1). The verdict is read from the TEST OUTPUT.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalValidation.cpp"
$hpp = "$root\include\bettercad\thermal\ThermalValidation.hpp"
$progress = "$s\val-mutation-progress.txt"
$only = $args
$probes = @(
 @{ Id='VAL01'; File=$cpp; Desc='an inverted/degenerate tetrahedron is downgraded to a warning'; Find="    case ValidationCode::MeshQualityPolicyUnusable:`n    case ValidationCode::ElementOutsideQualifiedEnvelope:`n        return ValidationSeverity::Warning;"; Replace="    case ValidationCode::MeshStructurallyInvalid:`n    case ValidationCode::MeshQualityPolicyUnusable:`n    case ValidationCode::ElementOutsideQualifiedEnvelope:`n        return ValidationSeverity::Warning;" },
 @{ Id='VAL02'; File=$cpp; Desc='a stale mesh is downgraded to information'; Find="    case ValidationCode::NoLoadsConfigured:`n    case ValidationCode::NoBoundaryConditionsConfigured:`n    case ValidationCode::TransientWithoutDatum:`n    case ValidationCode::InitialConditionOverriddenByPrescription:`n        return ValidationSeverity::Info;"; Replace="    case ValidationCode::MeshStale:`n    case ValidationCode::NoLoadsConfigured:`n    case ValidationCode::NoBoundaryConditionsConfigured:`n    case ValidationCode::TransientWithoutDatum:`n    case ValidationCode::InitialConditionOverriddenByPrescription:`n        return ValidationSeverity::Info;" },
 @{ Id='VAL03'; File=$cpp; Desc='a value on a bound is on the BAD side (non-strict comparison)'; Find="return higherIsBetter ? value < bound : value > bound;"; Replace="return higherIsBetter ? value <= bound : value >= bound;" },
 @{ Id='VAL04'; File=$cpp; Desc='a non-finite worst metric is no longer an error'; Find="        if (!finite(worst)) {"; Replace="        if (false) {" },
 @{ Id='VAL05'; File=$cpp; Desc='the failure bound is judged against the warning bound (warnings become errors)'; Find="if (limit.failure.has_value() && pastBound(worst, *limit.failure, higherIsBetter)) {"; Replace="if (limit.warning.has_value() && pastBound(worst, *limit.warning, higherIsBetter)) {" },
 @{ Id='VAL06'; File=$cpp; Desc='the aspect-ratio ceiling is removed'; Find="meshing::QualityThreshold{.warning = 81.0, .failure = kThermalAspectRatioCeiling}"; Replace="meshing::QualityThreshold{.warning = 81.0, .failure = std::nullopt}" },
 @{ Id='VAL07'; File=$cpp; Desc='the radius-ratio floor is removed'; Find="meshing::QualityThreshold{.warning = 3.0e-4, .failure = kThermalRadiusRatioFloor}"; Replace="meshing::QualityThreshold{.warning = 3.0e-4, .failure = std::nullopt}" },
 @{ Id='VAL08'; File=$cpp; Desc='the count of elements past the failure bound is reported as 1'; Find="finding.count = violators(*limit.failure);"; Replace="finding.count = 1;" },
 @{ Id='VAL09'; File=$cpp; Desc='the worst value is taken from the wrong end of the summary'; Find="const double worst = higherIsBetter ? summary.minimum : summary.maximum;"; Replace="const double worst = higherIsBetter ? summary.maximum : summary.minimum;" },
 @{ Id='VAL10'; File=$cpp; Desc='a structurally invalid mesh is not reported'; Find="    if (!quality.structurallyValid) {"; Replace="    if (false) {" },
 @{ Id='VAL11'; File=$cpp; Desc='invalid elements are not reported'; Find="    if (quality.invalidElements > 0) {"; Replace="    if (false) {" },
 @{ Id='VAL12'; File=$cpp; Desc='a contradictory reporting policy is silently ignored'; Find="    if (quality.thresholdPolicyError.has_value()) {"; Replace="    if (false) {" },
 @{ Id='VAL13'; File=$cpp; Desc='a steady analysis requires rho and cp'; Find="thermalMaterialProblem(document, body, mode)) {"; Replace="thermalMaterialProblem(document, body, ThermalAnalysisMode::Transient)) {" },
 @{ Id='VAL14'; File=$cpp; Desc='a transient analysis does not require rho and cp'; Find="thermalMaterialProblem(document, body, mode)) {"; Replace="thermalMaterialProblem(document, body, ThermalAnalysisMode::Steady)) {" },
 @{ Id='VAL15'; File=$cpp; Desc='at least one load is required'; Find="add(make(ValidationCode::NoLoadsConfigured,"; Replace="add(make(ValidationCode::LoadsInvalid," },
 @{ Id='VAL16'; File=$cpp; Desc='at least one boundary condition is required'; Find="add(make(ValidationCode::NoBoundaryConditionsConfigured,"; Replace="add(make(ValidationCode::BoundaryConditionsInvalid," },
 @{ Id='VAL17'; File=$cpp; Desc='an unresolved load is not reported'; Find="        if (!freshLoads.has_value()) {"; Replace="        if (false) {" },
 @{ Id='VAL18'; File=$cpp; Desc='an unresolved or conflicting boundary condition is not reported'; Find="if (const std::optional<BoundaryFailure> why = thermalBoundaryFailure(model, *currentMap, conditions)) {"; Replace="if (const std::optional<BoundaryFailure> why = std::optional<BoundaryFailure>{}) {" },
 @{ Id='VAL19'; File=$cpp; Desc='a stale assembled system is accepted'; Find="stale(ValidationSubject::System, requireCurrent(*derived.system, document, model, mode, loads, conditions));"; Replace="(void)0;" },
 @{ Id='VAL20'; File=$cpp; Desc='a numbering of another mesh generation is accepted'; Find="if (derived.dofMap != nullptr && !derived.dofMap->describes(model.mesh())) {"; Replace="if (false) {" },
 @{ Id='VAL21'; File=$cpp; Desc='stale prepared loads are accepted'; Find="stale(ValidationSubject::PreparedLoads, requireCurrent(*derived.preparedLoads, model, loads));"; Replace="(void)0;" },
 @{ Id='VAL22'; File=$cpp; Desc='a stale prepared boundary is accepted'; Find="stale(ValidationSubject::PreparedBoundary, requireCurrent(*derived.preparedBoundary, model, conditions));"; Replace="(void)0;" },
 @{ Id='VAL23'; File=$cpp; Desc='a stale steady solution is accepted'; Find="stale(ValidationSubject::Solution, requireCurrent(*steady, document, model, loads, conditions, definition.solver));"; Replace="(void)0;" },
 @{ Id='VAL24'; File=$cpp; Desc='a stale steady recovered result is accepted'; Find="stale(ValidationSubject::PostResult, requireCurrent(*derived.post, document, model, loads, conditions, definition.solver));"; Replace="(void)0;" },
 @{ Id='VAL25'; File=$cpp; Desc='a stale steady balance is accepted'; Find="stale(ValidationSubject::BalanceResult, requireCurrent(*derived.balance, document, model, loads, conditions, definition.solver));"; Replace="(void)0;" },
 @{ Id='VAL26'; File=$cpp; Desc='a solution from another assembled system is accepted'; Find="if (derived.system != nullptr && !(steady->source().system == derived.system->source())) {"; Replace="if (false) {" },
 @{ Id='VAL27'; File=$cpp; Desc='a steady solution for a transient analysis is not a mode mismatch'; Find="wrongMode(ValidationSubject::Solution, `"a steady solution was presented for a transient analysis`");"; Replace="(void)0;" },
 @{ Id='VAL28'; File=$cpp; Desc='recovered fields of another solution are accepted'; Find="if (solutionSource.has_value() && !(derived.post->source() == *solutionSource)) {"; Replace="if (false) {" },
 @{ Id='VAL29'; File=$cpp; Desc='a pure-Neumann steady model is accepted'; Find="    if (analysis.unanchored == 0) {"; Replace="    if (true) {" },
 @{ Id='VAL30'; File=$cpp; Desc='the steady datum rule is applied to a transient'; Find="        if (!transientMode) {`n            if (system->capacity().has_value()) {"; Replace="        if (true) {`n            if (system->capacity().has_value()) {" },
 @{ Id='VAL31'; File=$cpp; Desc='no free equation (fully constrained) is a failure'; Find="    if (equations.freeCount() == 0) {"; Replace="    if (equations.freeCount() == 0) {`n        findings.push_back(make(ValidationCode::NoTemperatureDatum, `"no free equation`"));" },
 @{ Id='VAL32'; File=$cpp; Desc='a missing transient capacity is not noticed'; Find="    if (!capacity.has_value()) {"; Replace="    if (false) {" },
 @{ Id='VAL33'; File=$cpp; Desc='a zero capacity on the diagonal is accepted'; Find="!(diagonal > 0.0)"; Replace="diagonal < 0.0" },
 @{ Id='VAL34'; File=$cpp; Desc='the recorded physical step residual is not judged'; Find="if (record.physicalResidual > tolerance) {"; Replace="if (false) {" },
 @{ Id='VAL35'; File=$cpp; Desc='the recorded reduced step residual is not judged'; Find="if (record.reducedResidual > tolerance) {"; Replace="if (false) {" },
 @{ Id='VAL36'; File=$cpp; Desc='the nominal step size is used where the actual one belongs'; Find="const double dt = grid.stepAt(k - 1);"; Replace="const double dt = grid.nominalStep();" },
 @{ Id='VAL37'; File=$cpp; Desc='no step is ever re-evaluated independently'; Find="if (previous.empty() || next.empty()) {"; Replace="if (true) {" },
 @{ Id='VAL38'; File=$cpp; Desc='the balance tolerance is not applied to the numbers'; Find="const bool within = balanceWithinTolerance(state.residual(), state.grossScale(), state.arithmeticScale);"; Replace="const bool within = true;" },
 @{ Id='VAL39'; File=$cpp; Desc='a balance state that records itself out of tolerance is accepted'; Find="} else if (state.status != BalanceStatus::Balanced) {"; Replace="} else if (false) {" },
 @{ Id='VAL40'; File=$cpp; Desc='a non-finite balance term falls through to the comparison'; Find="        if (!allFinite) {"; Replace="        if (false) {" },
 @{ Id='VAL41'; File=$cpp; Desc='the number of balance states is not checked'; Find="    if (states.size() != expectedStates) {"; Replace="    if (false) {" },
 @{ Id='VAL42'; File=$cpp; Desc='the finiteness of the temperatures is not checked'; Find="if (!isAbsoluteTemperature(state[i])) {"; Replace="if (false) {" },
 @{ Id='VAL43'; File=$cpp; Desc='a state of the wrong size is read anyway'; Find="        if (state.size() != expectedDofs) {"; Replace="        if (false) {" },
 @{ Id='VAL44'; File=$cpp; Desc='recovered temperatures are not compared with the solver''s'; Find="same = std::bit_cast<std::uint64_t>(solved[i].si()) == std::bit_cast<std::uint64_t>(state.temperatures[i].si());"; Replace="same = true;" },
 @{ Id='VAL45'; File=$cpp; Desc='a missing element result is not a cardinality failure'; Find="        if (!cardinalityOk) {"; Replace="        if (false) {" },
 @{ Id='VAL46'; File=$cpp; Desc='a recorded solve failure is ignored'; Find="    if (e.lastSolveFailure != nullptr) {"; Replace="    if (false) {" },
 @{ Id='VAL47'; File=$cpp; Desc='an absent solution is accepted'; Find="    if (e.states.empty()) {"; Replace="    if (false) {" },
 @{ Id='VAL48'; File=$cpp; Desc='the history contract is not enforced'; Find="        if (!contract) {"; Replace="        if (false) {" },
 @{ Id='VAL49'; File=$cpp; Desc='the steady pivot floor is ignored'; Find="if (pivotRatio.has_value() && (!finite(*pivotRatio) || *pivotRatio < settings.pivotFloor)) {"; Replace="if (false) {" },
 @{ Id='VAL50'; File=$cpp; Desc='the transient pivot floor is ignored'; Find="if (record.kernelInvoked && record.pivotRatio < settings.pivotFloor) {"; Replace="if (false) {" },
 @{ Id='VAL51'; File=$cpp; Desc='a transient balance is expected to have one state'; Find="const std::size_t expectedStates = transient ? e.grid->stepCount() : 1;"; Replace="const std::size_t expectedStates = 1;" },
 @{ Id='VAL52'; File=$cpp; Desc='a second residual tolerance in the transient check'; Find="    const double tolerance = settings.relativeResidualTolerance;"; Replace="    const double tolerance = 1.0e-3;" },
 @{ Id='VAL53'; File=$cpp; Desc='a failed transient setup no longer blocks the derived rules'; Find="firstBlocker(report, ValidationRule::AnalysisDefinition, ValidationRule::TransientSetup)"; Replace="firstBlocker(report, ValidationRule::AnalysisDefinition, ValidationRule::BoundaryConditions)" },
 @{ Id='VAL54'; File=$cpp; Desc='a source-compatibility failure no longer blocks well-posedness'; Find="    if (ruleFailedOrSkipped(report, ValidationRule::SourceCompatibility)) {"; Replace="    if (false) {" },
 @{ Id='VAL55'; File=$cpp; Desc='a well-posedness failure no longer blocks the solve rules'; Find="    if (ruleFailedOrSkipped(report, ValidationRule::WellPosed)) {"; Replace="    if (false) {" },
 @{ Id='VAL56'; File=$cpp; Desc='a report under another policy is still current'; Find="    if (report.policyFingerprint != thermalValidationPolicyFingerprint()) {"; Replace="    if (false) {" },
 @{ Id='VAL57'; File=$cpp; Desc='the recorded attribution check is skipped'; Find="if (haveLoads == loads && state.convections.size() == convections && state.reactions.size() == reactions) {"; Replace="if (true) {" },
 @{ Id='VAL58'; File=$cpp; Desc='the findings are not ordered by rule first'; Find="return std::tuple{static_cast<int>(a.rule), severityRank(a.severity),"; Replace="return std::tuple{severityRank(a.severity), static_cast<int>(a.rule)," },
 @{ Id='VAL59'; File=$cpp; Desc='the steady residual is judged against a tolerance 1e12 too loose'; Find="    const std::expected<SteadyResidual, SteadySolveFailure> judged =`n        validateSteadySolution(conductance, heat, equations, constraints, temperatures, settings);"; Replace="    ThermalSolverSettings loosened = settings;`n    loosened.relativeResidualTolerance *= 1.0e12;`n    const std::expected<SteadyResidual, SteadySolveFailure> judged =`n        validateSteadySolution(conductance, heat, equations, constraints, temperatures, loosened);" },
 @{ Id='VAL60'; File=$cpp; Desc='the transient step is re-evaluated against a tolerance 1e15 too loose'; Find="validateTransientStep(conductance, capacity, heat, equations, constraints, previous, next, dt, settings);"; Replace="validateTransientStep(conductance, capacity, heat, equations, constraints, previous, next, dt, ThermalSolverSettings{.relativeResidualTolerance = 1.0e6, .pivotFloor = settings.pivotFloor});" },
 @{ Id='VAL61'; File=$cpp; Desc='the balance attribution is not wired into the decision'; Find="        if (e.balanceCategories.has_value()) {"; Replace="        if (false) {" },
 @{ Id='VAL62'; File=$cpp; Desc='the recovered fields are not judged'; Find="addAll(checkPostStates(*e.post, e.tetCount, e.dofCount, e.states));"; Replace="(void)0;" },
 @{ Id='VAL63'; File=$cpp; Desc='the temperature finiteness is not wired into the decision'; Find="addAll(checkTemperatureStates(e.states, e.dofCount, e.times));"; Replace="(void)0;" },
 @{ Id='VAL64'; File=$cpp; Desc='the steady residual verdict is not wired into the decision'; Find="        report.metrics.steadyResidual = verdict.residual;`n        addAll(std::move(verdict.findings));"; Replace="        report.metrics.steadyResidual = verdict.residual;" },
 @{ Id='VAL65'; File=$cpp; Desc='the transient residual verdict is not wired into the decision'; Find="        report.metrics.worstTransientResidual = verdict.worstResidual;`n        addAll(std::move(verdict.findings));"; Replace="        report.metrics.worstTransientResidual = verdict.worstResidual;" },
 @{ Id='VAL66'; File=$cpp; Desc='the balance verdict is not wired into the decision'; Find="        report.metrics.worstBalanceResidual = verdict.worstResidual;`n        addAll(std::move(verdict.findings));"; Replace="        report.metrics.worstBalanceResidual = verdict.worstResidual;" },
 @{ Id='VAL67'; File=$cpp; Desc='a stale transient solution is accepted'; Find="stale(ValidationSubject::Solution, requireCurrent(*transient,"; Replace="stale(ValidationSubject::Solution, Result<void>{}); (void)(requireCurrent(*transient," },
 @{ Id='VAL68'; File=$cpp; Desc='stale transient recovered fields are accepted'; Find="stale(ValidationSubject::PostResult, requireCurrent(*derived.post, document, model, loads, conditions,`n"; Replace="stale(ValidationSubject::PostResult, Result<void>{}); (void)(requireCurrent(*derived.post, document, model, loads, conditions,`n" },
 @{ Id='VAL69'; File=$cpp; Desc='a stale transient balance is accepted'; Find="stale(ValidationSubject::BalanceResult, requireCurrent(*derived.balance, document, model, loads, conditions,`n"; Replace="stale(ValidationSubject::BalanceResult, Result<void>{}); (void)(requireCurrent(*derived.balance, document, model, loads, conditions,`n" },
 @{ Id='VAL70'; File=$cpp; Desc='the FinalOnly explanation is dropped'; Find="request EveryStep"; Replace="request more" },
 @{ Id='VAL71'; File=$hpp; Desc='a skipped rule counts as a pass in the overall status'; Find="            if (s == CheckStatus::Fail || s == CheckStatus::Skipped) {`n                return ValidationStatus::Fail;"; Replace="            if (s == CheckStatus::Fail) {`n                return ValidationStatus::Fail;" },
 @{ Id='VAL72'; File=$hpp; Desc='accepted() refuses a warning'; Find="[[nodiscard]] bool accepted() const noexcept { return status() != ValidationStatus::Fail; }"; Replace="[[nodiscard]] bool accepted() const noexcept { return status() == ValidationStatus::Pass; }" },
 @{ Id='VAL73'; File=$hpp; Desc='solveAccepted() ignores the temperature finiteness'; Find="            if (check.rule > ValidationRule::ResultFinite) {"; Replace="            if (check.rule > ValidationRule::SolverResidual) {" },
 @{ Id='VAL74'; File=$hpp; Desc='errorCount() counts warnings too'; Find="findings, [](const ValidationFinding& f) { return f.severity == ValidationSeverity::Error; }));"; Replace="findings, [](const ValidationFinding& f) { return f.severity != ValidationSeverity::Info; }));" },
 @{ Id='VAL75'; File=$hpp; Desc='a warning makes the overall status Fail'; Find="            warning = warning || s == CheckStatus::Warning;`n        }`n        if (checks.empty()) {"; Replace="            if (s == CheckStatus::Warning) {`n                return ValidationStatus::Fail;`n            }`n        }`n        if (checks.empty()) {" }
)
if ($only.Count -gt 0) { $probes = $probes | Where-Object { $only -contains $_.Id } }
foreach ($p in $probes) {
    if ($env:VAL_DRYRUN) {
        $t = [IO.File]::ReadAllText($p.File)
        $n = ([regex]::Matches($t, [regex]::Escape($p.Find))).Count
        "{0} occurrences={1}  {2}" -f $p.Id, $n, $p.Desc
        continue
    }
    $before = [IO.File]::ReadAllText($p.File)
    $started = Get-Date
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-t.ps1" -Name $p.Id -File $p.File -Find $p.Find -Replace $p.Replace -Filter "[thermal][validation]" -TimeoutSeconds 420 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | {4} | {5:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8
