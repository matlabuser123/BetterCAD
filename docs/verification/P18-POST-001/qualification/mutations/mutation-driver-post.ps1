param([string]$Only = "", [string]$ProgressFile = "mutation-post-progress.txt")
$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$PP = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalPost.cpp"
$m = @()
function M($n,$desc,$find,$rep){ $script:m += ,@($n,$desc,$PP,'[thermal][post]',($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }

M 'PM01' 'the local node order is sorted by unknown index (not the tetrahedron''s own order)' '        table.dofs.push_back(*dofs);' @'
        std::array<ThermalDofIndex, 4> sorted = *dofs;
        std::sort(sorted.begin(), sorted.end(), [](ThermalDofIndex a, ThermalDofIndex b) { return a.value() < b.value(); });
        table.dofs.push_back(sorted);
'@
M 'PM02' 'temperatures are read through the raw NodeId (minus one) and not through the thermal numbering' '        table.dofs.push_back(*dofs);' @'
        table.dofs.push_back(std::array<ThermalDofIndex, 4>{
            ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(tet.nodes[0].value() - 1)),
            ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(tet.nodes[1].value() - 1)),
            ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(tet.nodes[2].value() - 1)),
            ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(tet.nodes[3].value() - 1))});
'@
M 'PM03' 'the y and z gradient components are swapped' '        const TemperatureGradient3 gradient = table.geometries[e].gradientFrom(local);' @'
        const TemperatureGradient3 gradient0 = table.geometries[e].gradientFrom(local);
        const TemperatureGradient3 gradient{.x = gradient0.x, .y = gradient0.z, .z = gradient0.y};
'@
M 'PM04' 'the x and y gradient components are swapped' '        const TemperatureGradient3 gradient = table.geometries[e].gradientFrom(local);' @'
        const TemperatureGradient3 gradient0 = table.geometries[e].gradientFrom(local);
        const TemperatureGradient3 gradient{.x = gradient0.y, .y = gradient0.x, .z = gradient0.z};
'@
M 'PM05' 'the Fourier sign is dropped: q = +k grad(T)' @'
        const HeatFlux3 flux{.x = HeatFlux{} - conductivity * gradient.x,
                             .y = HeatFlux{} - conductivity * gradient.y,
                             .z = HeatFlux{} - conductivity * gradient.z};
'@ @'
        const HeatFlux3 flux{.x = HeatFlux{} + conductivity * gradient.x,
                             .y = HeatFlux{} + conductivity * gradient.y,
                             .z = HeatFlux{} + conductivity * gradient.z};
'@
M 'PM06' 'the sign is dropped on the z component only' '.z = HeatFlux{} - conductivity * gradient.z};' '.z = HeatFlux{} + conductivity * gradient.z};'
M 'PM07' 'the conductivity is omitted: q = -grad(T) with k = 1' @'
        const HeatFlux3 flux{.x = HeatFlux{} - conductivity * gradient.x,
'@ @'
        const HeatFlux3 flux{.x = HeatFlux{} - ThermalConductivity::fromSi(1.0) * gradient.x,
'@
M 'PM08' 'the conductivity enters squared on y: q_y = -k^2 grad_y' '.y = HeatFlux{} - conductivity * gradient.y,' '.y = HeatFlux::fromSi(-conductivity.si() * conductivity.si() * gradient.y.si()),'
M 'PM09' 'the stored steady temperatures are in Celsius' '.temperatures = std::vector<Temperature>(solution.temperatures().begin(),
                                                                               solution.temperatures().end()),
                                      .fields = std::move(*fields)});
    // Published only here' '.temperatures = [&] { std::vector<Temperature> c(solution.temperatures().begin(), solution.temperatures().end());
                                                                for (Temperature& t : c) { t = Temperature::fromSi(t.si() - 273.15); }
                                                                return c; }(),
                                      .fields = std::move(*fields)});
    // Published only here'
M 'PM10' 'the gradient is computed per millimetre (a stray 1e-3)' '        if (!isFinite(gradient)) {' @'
        const TemperatureGradient3 gradientMm{.x = TemperatureGradient::fromSi(gradient.x.si() * 1e-3),
                                              .y = TemperatureGradient::fromSi(gradient.y.si() * 1e-3),
                                              .z = TemperatureGradient::fromSi(gradient.z.si() * 1e-3)};
        (void)gradientMm;
        if (!isFinite(gradient)) {
'@
M 'PM11' 'the magnitude is the L1 norm' '        const HeatFlux magnitude = flux.magnitude();' '        const HeatFlux magnitude = HeatFlux::fromSi(std::abs(flux.x.si()) + std::abs(flux.y.si()) + std::abs(flux.z.si()));'
M 'PM12' 'the magnitude is the largest component only' '        const HeatFlux magnitude = flux.magnitude();' '        const HeatFlux magnitude = HeatFlux::fromSi(std::max({std::abs(flux.x.si()), std::abs(flux.y.si()), std::abs(flux.z.si())}));'
M 'PM13' 'an invalid element is skipped instead of failing the whole recovery' '            return std::unexpected(failure(PostProblem::InvalidTetGeometry, ErrorCode::FailedPrecondition,
                                           std::format("element {}: {}", tet.id, geometry.error().message), tet.id));' '            continue;'
M 'PM14' 'the non-finite gradient gate is removed' '        if (!isFinite(gradient)) {' '        if (false && !isFinite(gradient)) {'
M 'PM15' 'the non-finite flux gate is removed' '        if (!isFinite(flux)) {' '        if (false && !isFinite(flux)) {'
M 'PM16' 'the non-finite magnitude gate is removed' '        if (!std::isfinite(magnitude.si())) {' '        if (false && !std::isfinite(magnitude.si())) {'
M 'PM17' 'a non-finite nodal temperature is accepted' '        if (!std::isfinite(temperatures[i].si())) {' '        if (false && !std::isfinite(temperatures[i].si())) {'
M 'PM18' 'a temperature below absolute zero is accepted' '        if (!isAbsoluteTemperature(temperatures[i])) {' '        if (false && !isAbsoluteTemperature(temperatures[i])) {'
M 'PM19' 'the temperature count is not checked against the numbering' '    if (temperatures.size() != static_cast<std::size_t>(map.dofCount())) {' '    if (false && temperatures.size() != static_cast<std::size_t>(map.dofCount())) {'
M 'PM20' 'a conductivity of zero or less is accepted' '    if (!std::isfinite(conductivity.si()) || !(conductivity.si() > 0.0)) {' '    if (!std::isfinite(conductivity.si())) {'
M 'PM21' 'the numbering is not checked against the mesh' '    if (!map.describes(mesh)) {' '    if (false && !map.describes(mesh)) {'
M 'PM22' 'a remesh with the same counts is accepted as the same mesh (stamp ignored)' '    if (!mesh.owns(system.mesh) || mesh.nodeCount() != system.nodeCount) {' '    if (mesh.nodeCount() != system.nodeCount) {'
M 'PM23' 'the solution is not required to be current at the model level (steady)' @'
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings);
        !current.has_value()) {
'@ @'
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings);
        false && !current.has_value()) {
'@
M 'PM24' 'the solution is not required to be current at the model level (transient)' @'
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, initial, transient, settings);
        !current.has_value()) {
'@ @'
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, initial, transient, settings);
        false && !current.has_value()) {
'@
M 'PM25' 'a model older than a material edit is paired with the new conductivity (cross-check removed)' '    if (!(model.conductivity() == material->conductivity())) {' '    if (false && !(model.conductivity() == material->conductivity())) {'
M 'PM26' 'the transient states are published in reverse order' '        states.push_back(ThermalPostState{.time = solution.timeOf(i),' '        states.insert(states.begin(), ThermalPostState{.time = solution.timeOf(i),'
M 'PM27' 'every transient state carries the first state''s time' '.time = solution.timeOf(i),' '.time = solution.timeOf(0),'
M 'PM28' 'a bad late state is reported as state 0' 'checkTemperatures(map, solution.temperaturesAt(i), i);' 'checkTemperatures(map, solution.temperaturesAt(i), 0);'
M 'PM29' 'an element is looked up by its raw id (minus one) and not by its ordinal' '    return *ordinal;
}' '    return static_cast<std::size_t>(element.value() - 1);
}'
M 'PM30' 'a zero gradient gives negative zero (the flux is written -(k g))' '.x = HeatFlux{} - conductivity * gradient.x,' '.x = -(conductivity * gradient.x),'
M 'PM31' 'a steady source records itself as transient' '    return ThermalPostSource{.mode = ThermalAnalysisMode::Steady,' '    return ThermalPostSource{.mode = ThermalAnalysisMode::Transient,'
M 'PM32' 'the result stores nodal temperatures by averaging element values (nodal smoothing of the gradient: each element gets the mean of itself and its predecessor)' '        out.gradients.push_back(gradient);' @'
        if (e > 0) {
            const TemperatureGradient3& p = out.gradients.back();
            out.gradients.push_back(TemperatureGradient3{.x = TemperatureGradient::fromSi(0.5 * (p.x.si() + gradient.x.si())),
                                                         .y = TemperatureGradient::fromSi(0.5 * (p.y.si() + gradient.y.si())),
                                                         .z = TemperatureGradient::fromSi(0.5 * (p.z.si() + gradient.z.si()))});
        } else {
            out.gradients.push_back(gradient);
        }
'@
M 'PM33' 'the temperatureOf lookup ignores the numbering the result belongs to' '    if (!describes(map)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this result belongs to another numbering, so a node cannot be looked up through it");
    }' ''
M 'PM34' 'the state index of temperatureOf is not range-checked' '    if (state >= states_.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("state {} is outside this result''s {} states", state, states_.size()));
    }
    const Result<ThermalDofIndex> dof' '    const Result<ThermalDofIndex> dof'
M 'PM35' 'a held steady result is judged current even when the model changed (stale reasons ignored)' @'
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the recovered fields no longer describe the model: {} changed. Solve and recover "
                                 "again",
                                 reasonsText(reasons)));
}

Result<void> requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const UniformInitialTemperature& initial,
'@ @'
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty() || true) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the recovered fields no longer describe the model: {} changed. Solve and recover "
                                 "again",
                                 reasonsText(reasons)));
}

Result<void> requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const UniformInitialTemperature& initial,
'@

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
