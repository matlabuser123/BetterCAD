param([string]$ProgressFile = "mutation-post2-progress.txt", [string]$Only = "")
$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$PP = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalPost.cpp"
$m = @()
function M($n,$desc,$find,$rep){ $script:m += ,@($n,$desc,$PP,'[thermal][post]',($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }

M 'PM02b' 'temperatures are read through the raw NodeId (minus one), clamped so the read stays in range' '        table.dofs.push_back(*dofs);' @'
        const auto clamped = [&](meshing::NodeId n) {
            return ThermalDofIndex::fromValue(std::min<ThermalDofIndex::ValueType>(
                static_cast<ThermalDofIndex::ValueType>(n.value() - 1), static_cast<ThermalDofIndex::ValueType>(map.dofCount() - 1)));
        };
        table.dofs.push_back(std::array<ThermalDofIndex, 4>{clamped(tet.nodes[0]), clamped(tet.nodes[1]), clamped(tet.nodes[2]), clamped(tet.nodes[3])});
'@
M 'PM10b' 'the gradient is computed per millimetre (a stray 1e-3 applied for real)' '        const TemperatureGradient3 gradient = table.geometries[e].gradientFrom(local);' @'
        const TemperatureGradient3 gradient0 = table.geometries[e].gradientFrom(local);
        const TemperatureGradient3 gradient{.x = TemperatureGradient::fromSi(gradient0.x.si() * 1e-3),
                                            .y = TemperatureGradient::fromSi(gradient0.y.si() * 1e-3),
                                            .z = TemperatureGradient::fromSi(gradient0.z.si() * 1e-3)};
'@
M 'PM29b' 'an element is looked up by its raw id (minus one), clamped to stay in range' '    return *ordinal;
}' '    return std::min<std::size_t>(static_cast<std::size_t>(element.value() - 1), result.stateCount() > 0 ? result.elementIds().size() - 1 : 0);
}'
M 'PM36' 'a held transient result is judged current even when the model changed (stale reasons ignored)' '        currentTransientSolutionSource(document, model, loads, conditions, initial, transient, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the recovered fields cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {' '        currentTransientSolutionSource(document, model, loads, conditions, initial, transient, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the recovered fields cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty() || true) {'
M 'PM37' 'a held steady result may be asked about with the transient settings (mode check removed)' '    if (result.mode() != ThermalAnalysisMode::Steady) {' '    if (false && result.mode() != ThermalAnalysisMode::Steady) {'
M 'PM38' 'a held transient result may be asked about without the transient settings (mode check removed)' '    if (result.mode() != ThermalAnalysisMode::Transient) {' '    if (false && result.mode() != ThermalAnalysisMode::Transient) {'
M 'PM39' 'the recovery does not refuse a solution whose conductivity is invalid at the seam (seam check skipped)' '    if (std::optional<PostFailure> refused = checkConductivity(conductivity); refused.has_value()) {
        return refused;
    }' '    (void)conductivity;'
M 'PM40' 'the history is checked only through the first state (late states are not pre-checked)' '    for (std::size_t i = 0; i < solution.sampleCount(); ++i) {
        if (std::optional<PostFailure> refused = checkTemperatures(' '    for (std::size_t i = 0; i < std::min<std::size_t>(solution.sampleCount(), 1); ++i) {
        if (std::optional<PostFailure> refused = checkTemperatures('

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
