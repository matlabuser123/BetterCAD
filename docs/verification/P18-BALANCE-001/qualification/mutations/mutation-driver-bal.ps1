param([string]$Only = "", [string]$ProgressFile = "mutation-bal-progress.txt")
$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$BC = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalBalance.cpp"
$BH = "C:\Users\hasib\OneDrive\Desktop\CAD\include\bettercad\thermal\ThermalBalance.hpp"
$m = @()
function M($n,$desc,$file,$find,$rep){ $script:m += ,@($n,$desc,$file,'[thermal][balance]',($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }

M 'BAL01' 'the reaction sign is flipped: R = -r' $BC '        const double heat = fullResidual[dof];' '        const double heat = -fullResidual[dof];'
M 'BAL02' 'the reaction is defined as whatever closes the balance (the remainder)' $BC '    out.terms.prescribedTemperatureReaction = Power::fromSi(reactionInto);' '    out.terms.prescribedTemperatureReaction = Power::fromSi(-(categories.surfaceInto + categories.generationInto + categories.convectionInto) + (stored.has_value() ? stored->si() / timeStep : 0.0));'
M 'BAL03' 'the free rows are counted as reactions too' $BC '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    return out;' '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    for (std::size_t i = 0; i < fullResidual.size(); ++i) {
        if (!constraints.isConstrained(ThermalDofIndex::fromValue(i))) {
            out.push_back(ReactionHeat{.dof = ThermalDofIndex::fromValue(i), .heatInto = Power::fromSi(fullResidual[i])});
        }
    }
    return out;'
M 'BAL04' 'each prescribed unknown is counted twice' $BC '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    return out;' '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    return out;'
M 'BAL05' 'the convection is the ambient source Qconv alone' $BC '        const std::array<Power, kTri3Nodes> parts = facet.term.residualFrom(face);
        out += parts[0].si() + parts[1].si() + parts[2].si();' '        (void)face;
        out += facet.term.source(0).si() + facet.term.source(1).si() + facet.term.source(2).si();'
M 'BAL06' 'the convection sign is reversed (outward counted as into the body)' $BC '                          .heatInto = Power::fromSi(-out)};' '                          .heatInto = Power::fromSi(out)};'
M 'BAL07' 'the surface load totals are made absolute' $BC '        const LoadHeat entry{.load = load.load, .heatInto = load.integrated.heatRate};' '        const LoadHeat entry{.load = load.load, .heatInto = Power::fromSi(std::abs(load.integrated.heatRate.si()))};'
M 'BAL08' 'the generation totals are made absolute' $BC '            out.generation.push_back(entry);
            out.generationInto += entry.heatInto.si();' '            out.generation.push_back(entry);
            out.generationInto += std::abs(entry.heatInto.si());'
M 'BAL09' 'the global heat input is added on top of the explicit categories' $BC '    const double arithmetic = absoluteProduct(input.conductance, t) + absoluteSum(input.heat.values());' '    categories->surfaceInto += input.heat.total().si();
    const double arithmetic = absoluteProduct(input.conductance, t) + absoluteSum(input.heat.values());'
M 'BAL10' 'the transient residual omits the storage term C dT/dt' $BC '        r[i] = (*cdt)[i] / timeStep + (*kt)[i] - q[i];' '        r[i] = (*cdt)[i] * 0.0 / timeStep + (*kt)[i] - q[i];'
M 'BAL11' 'the stored energy uses diag(C) only' $BC '    double sum = 0.0;
    for (const double row : *cdt) {
        sum += row;
    }' '    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += capacity.coeffSi(i, i) * change[i];
    }'
M 'BAL12' 'the stored energy leaves out the prescribed unknowns' $BC '    std::expected<Energy, BalanceFailure> stored = storedEnergyChange(*input.capacity, previous, next);' '    std::vector<Temperature> nextFree(next.begin(), next.end());
    for (const ThermalTemperatureConstraint& c : input.constraints.constrained()) {
        nextFree[static_cast<std::size_t>(c.dof.value())] = previous[static_cast<std::size_t>(c.dof.value())];
    }
    std::expected<Energy, BalanceFailure> stored = storedEnergyChange(*input.capacity, previous, nextFree);'
M 'BAL13' 'the nominal step is used where the actual one belongs' $BC '        const double dt = grid.stepAt(k - 1);' '        const double dt = grid.nominalStep();'
M 'BAL14' 'the convection of a step is evaluated at T[n] instead of T[n+1]' $BC '    std::expected<CategoryTotals, BalanceFailure> categories = categoriesOf(input, next, step);' '    std::expected<CategoryTotals, BalanceFailure> categories = categoriesOf(input, previous, step);'
M 'BAL15' 'an output interval is accepted as an integration step (FinalOnly over many steps)' $BC '    const bool singleStep = steps == 1 && solution.sampleCount() == 1;' '    const bool singleStep = solution.sampleCount() == 1;'
M 'BAL16' 'the first convection condition is skipped' $BC '    for (const PreparedConvectionBoundary& condition : input.convections) {
        std::expected<ConvectionHeat, BalanceFailure> exchanged' '    for (const PreparedConvectionBoundary& condition : input.convections) {
        if (&condition == input.convections.data()) {
            continue;
        }
        std::expected<ConvectionHeat, BalanceFailure> exchanged'
M 'BAL17' 'the first load is skipped' $BC '    for (const ThermalLoadContribution& load : input.loads) {
        const LoadHeat entry' '    for (const ThermalLoadContribution& load : input.loads) {
        if (&load == input.loads.data()) {
            continue;
        }
        const LoadHeat entry'
M 'BAL18' 'the gross scale is the category-level scale (cancelling loads vanish from it)' $BC 'Power ThermalHeatBalance::grossScale() const noexcept {' 'Power ThermalHeatBalance::grossScale() const noexcept {
    if (true) {
        return terms.scale();
    }'
M 'BAL19' 'a non-finite residual component is accepted (steady)' $BC '    if (!allFinite(r)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                       "a component of the full residual K T - Q is not finite"));' '    if (false && !allFinite(r)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                       "a component of the full residual K T - Q is not finite"));'
M 'BAL20' 'the relative tolerance is loosened to 1e-3' $BH 'inline constexpr double kBalanceRelativeTolerance = 1.0e-13;' 'inline constexpr double kBalanceRelativeTolerance = 1.0e-3;'
M 'BAL21' 'the convection conditions are listed in reverse order' $BC '    if (!std::isfinite(out.surfaceInto) || !std::isfinite(out.generationInto) || !std::isfinite(out.convectionInto)) {' '    std::ranges::reverse(out.convections);
    if (!std::isfinite(out.surfaceInto) || !std::isfinite(out.generationInto) || !std::isfinite(out.convectionInto)) {'
M 'BAL22' 'the attribution check is skipped' $BC '    const double scale = absoluteSum(input.heat.values()) + std::abs(attributed);' '    const double scale = absoluteSum(input.heat.values()) + std::abs(attributed);
    if (scale >= 0.0) {
        return std::nullopt;
    }'
M 'BAL23' 'the mode of the system is not checked at the seam' $BC '    if (system.source().mode != mode) {' '    if (false && system.source().mode != mode) {'
M 'BAL24' 'a prescribed unknown that does not hold its temperature is accepted' $BC '        if (dof >= state.size() || !(state[dof].si() == constraint.temperature.si())) {' '        if (false && (dof >= state.size() || !(state[dof].si() == constraint.temperature.si()))) {'
M 'BAL25' 'the system is not compared with the solution''s own (steady seam)' $BC '    if (!(solution.source().system == system.source())) {
        return std::unexpected(failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                                       "the solution was not computed from this assembled system"));' '    if (false && !(solution.source().system == system.source())) {
        return std::unexpected(failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                                       "the solution was not computed from this assembled system"));'
M 'BAL26' 'the steady solution is not required to be current' $BC '    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings); !current.has_value()) {' '    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings); false && !current.has_value()) {'
M 'BAL27' 'the assembled system is not required to be current' $BC '    if (Result<void> current = requireCurrent(system, document, model, mode, loads, conditions); !current.has_value()) {' '    if (Result<void> current = requireCurrent(system, document, model, mode, loads, conditions); false && !current.has_value()) {'
M 'BAL28' 'the prepared loads are not required to be current' $BC '    if (Result<void> current = requireCurrent(preparedLoads, model, loads); !current.has_value()) {' '    if (Result<void> current = requireCurrent(preparedLoads, model, loads); false && !current.has_value()) {'
M 'BAL29' 'the prepared boundary is not required to be current' $BC '    if (Result<void> current = requireCurrent(preparedBoundary, model, conditions); !current.has_value()) {' '    if (Result<void> current = requireCurrent(preparedBoundary, model, conditions); false && !current.has_value()) {'
M 'BAL30' 'the prepared convection conditions are not compared with the system''s' $BC '    if (!same) {' '    if (false && !same) {'
M 'BAL31' 'a transient balance may be asked about as steady (mode check removed)' $BC '    if (result.mode() != ThermalAnalysisMode::Steady) {' '    if (false && result.mode() != ThermalAnalysisMode::Steady) {'
M 'BAL32' 'a steady balance may be asked about as transient (mode check removed)' $BC '    if (result.mode() != ThermalAnalysisMode::Transient) {' '    if (false && result.mode() != ThermalAnalysisMode::Transient) {'
M 'BAL33' 'a held balance is always judged current (stale reasons ignored)' $BC '    if (reasons.empty()) {' '    if (reasons.empty() || true) {'
M 'BAL34' 'the storage rate divides by 1 s instead of the step' $BC '        out.terms.storedEnergyRate = Power::fromSi(stored->si() / timeStep);' '        out.terms.storedEnergyRate = Power::fromSi(stored->si() / 1.0);'
M 'BAL35' 'the stored energy has the wrong sign' $BC '    return Energy::fromSi(sum);' '    return Energy::fromSi(-sum);'
M 'BAL36' 'the status is always Balanced' $BC '    out.status = balanceWithinTolerance(residual, gross, out.arithmeticScale) ? BalanceStatus::Balanced
                                                                              : BalanceStatus::OutOfTolerance;' '    out.status = BalanceStatus::Balanced;'
M 'BAL37' 'the steady residual is K T + Q' $BC '        r[i] = (*kt)[i] - q[i];' '        r[i] = (*kt)[i] + q[i];'
M 'BAL38' 'the arithmetic scale is dropped from the tolerance' $BC '    const double arithmetic = absoluteProduct(input.conductance, t) + absoluteSum(input.heat.values());' '    const double arithmetic = 0.0 * absoluteProduct(input.conductance, t) + absoluteSum(input.heat.values()) * 0.0;'
M 'BAL39' 'the integrated external energy uses the nominal step' $BC '        external += dt * into;' '        external += grid.nominalStep() * into;'
M 'BAL40' 'the step number is not recorded' $BC '        out.step = step;' '        out.step = 0;'
M 'BAL41' 'a step is stamped with the time of its start' $BC '        state->time = Time::fromSi(grid.timeAt(k));' '        state->time = Time::fromSi(grid.timeAt(k - 1));'
M 'BAL42' 'the reactions are listed in reverse order' $BC '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    return out;' '        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    std::ranges::reverse(out);
    return out;'
M 'BAL43' 'surface and generation loads are swapped' $BC '        if (load.kind == ThermalLoadKind::SurfaceHeatFlux) {' '        if (load.kind == ThermalLoadKind::VolumetricHeatSource) {'
M 'BAL44' 'balanced() ignores the state statuses' $BC '        if (state.status != BalanceStatus::Balanced) {' '        if (false && state.status != BalanceStatus::Balanced) {'
M 'BAL45' 'the steady balance accepts a capacity-bearing algebra (mode check removed)' $BC '    if (input.capacity != nullptr) {
        return std::unexpected(failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                                       "a steady balance was asked of an algebra that carries a capacity"));
    }' '    if (false) {
        return std::unexpected(failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                                       "a steady balance was asked of an algebra that carries a capacity"));
    }'
M 'BAL46' 'a transient step balance does not validate the time step' $BC '    if (!(timeStep > 0.0) || !std::isfinite(timeStep)) {
        return std::unexpected(failure(BalanceProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep), step));
    }' '    if (false) {
        return std::unexpected(failure(BalanceProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep), step));
    }'
M 'BAL47' 'a time step in the residual is not validated' $BC '    if (!(timeStep > 0.0) || !std::isfinite(timeStep)) {
        return std::unexpected(failure(BalanceProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }' '    if (false) {
        return std::unexpected(failure(BalanceProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }'

if ($Only) { $m = $m | Where-Object { $_[0] -in ($Only -split ',') } }
"" | Out-File "$scr\$ProgressFile"
foreach ($e in $m) {
    $name = $e[0]
    & "$scr\mutate-t.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\$ProgressFile" -Append
}
"DONE" | Out-File "$scr\$ProgressFile" -Append
