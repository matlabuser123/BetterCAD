$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSolve.cpp"
$LS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\linalg\SymmetricSolve.cpp"
$WS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\structural\StructuralSolve.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[thermal][solve],[linalg]"
M 'S01' 'beff = Qf (the Kfc Tc term omitted)' $TS $F 'out.beff_[equation] = out.qf_[equation] - coupled;' 'out.beff_[equation] = out.qf_[equation];'
M 'S02' 'prescribed temperatures forced to zero' $TS $F 'out.tc_.push_back(constraint.temperature.si());' 'out.tc_.push_back(0.0);'
M 'S03' 'Kfc Tc added instead of subtracted' $TS $F 'out.beff_[equation] = out.qf_[equation] - coupled;' 'out.beff_[equation] = out.qf_[equation] + coupled;'
M 'S04' 'free equations assumed to be the first Nfree unknowns' $TS $F 'const auto globalRow = static_cast<std::size_t>(freeDofs[equation].value());' 'const auto globalRow = equation;'
M 'S05' 'prescribed ordinal reversed in Kfc' $TS $F 'const std::size_t ordinal = ordinalOf[globalColumn];' 'const std::size_t ordinal = prescribed.size() - 1 - ordinalOf[globalColumn];'
M 'S06' 'Nfree = 0 reported as singular' $TS $F "    std::vector<Temperature> free(reduced->rows());`n    if (reduced->rows() > 0) {" "    std::vector<Temperature> free(reduced->rows());`n    if (reduced->rows() == 0) {`n        return std::unexpected(failure(SteadySolveProblem::SingularSystem, ErrorCode::FailedPrecondition, `"no free equations`"));`n    }`n    if (reduced->rows() > 0) {"
M 'S07' 'at least one prescribed temperature required' $TS $F "    ThermalAlgebraicSolution out;`n    out.freeEquations = reduced->rows();" "    if (constraints.isEmpty()) {`n        return std::unexpected(failure(SteadySolveProblem::MissingTemperatureDatum, ErrorCode::FailedPrecondition, `"no Dirichlet`"));`n    }`n    ThermalAlgebraicSolution out;`n    out.freeEquations = reduced->rows();"
M 'S08' 'epsilon added to the reduced diagonal' $TS $F 'out.values_.push_back(values[slot]);' 'out.values_.push_back(values[slot] + (freeOf[globalColumn] == equation ? 1.0e-9 : 0.0));'
M 'S09' 'first free unknown pinned by a large diagonal' $TS $F 'out.values_.push_back(values[slot]);' 'out.values_.push_back(values[slot] + ((equation == 0 && freeOf[globalColumn] == 0) ? 1.0e12 : 0.0));'
M 'S10' 'anchoring analysis does not refuse an unanchored region' $TS $F 'if (out.anchoring.unanchored > 0) {' 'if (out.anchoring.unanchored > 100000000) {'
M 'S11' 'full-system residual (reactions) treated as a free-equation failure' $TS $F 'out.fullResidual = std::move(*residual);' "out.fullResidual = std::move(*residual);`n    for (const double r : out.fullResidual) {`n        if (std::abs(r) > 1.0e-9) {`n            return std::unexpected(failure(SteadySolveProblem::ResidualTooLarge, ErrorCode::FailedPrecondition, `"full residual`"));`n        }`n    }"
M 'S12' 'stale assembled system accepted' $TS $F "        !current.has_value()) {`n        return std::unexpected(`n            SteadySolveFailure{.problem = SteadySolveProblem::StaleSystem" "        false && !current.has_value()) {`n        return std::unexpected(`n            SteadySolveFailure{.problem = SteadySolveProblem::StaleSystem"
M 'S13' 'stale prepared boundary accepted' $TS $F "        !stale.empty()) {`n        std::string names;`n        for (const BoundaryStaleReason reason : stale) {" "        false && !stale.empty()) {`n        std::string names;`n        for (const BoundaryStaleReason reason : stale) {"
M 'S14' 'solver settings dropped from the solution source' $TS $F '.temperatures = std::move(temperatureConditions),
                                                      .settings = settings},' '.temperatures = std::move(temperatureConditions),
                                                      .settings = ThermalSolverSettings{}},'
M 'S15' 'prescribed conditions dropped from the solution source' $TS $F '.temperatures = std::move(temperatureConditions),' '.temperatures = {},'
M 'S16' 'settings ignored by the staleness comparison' $TS $F 'if (!(solved.settings == current.settings)) {' 'if (!(solved.settings == current.settings) && current.system.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'S17' 'prescribed temperatures ignored by the staleness comparison' $TS $F 'if (!(solved.temperatures == current.temperatures)) {' 'if (!(solved.temperatures == current.temperatures) && current.system.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'S18' 'a constrained temperature perturbed after reconstruction' $TS $F 'out.temperatures = std::move(*full);' "out.temperatures = std::move(*full);`n    if (!constraints.isEmpty()) {`n        const auto d = static_cast<std::size_t>(constraints.constrained()[0].dof.value());`n        out.temperatures[d] = Temperature::fromSi(std::nextafter(out.temperatures[d].si(), 1.0e30));`n    }"
M 'S19' 'rounding noise counts as an anchor (tolerance zero)' $TS $F 'anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;' 'anchoredRow[row] = std::abs(sum) > 0.0;'
M 'S20' 'a weak real anchor is not recognised (tolerance 1e-3)' $TS $F 'anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;' 'anchoredRow[row] = std::abs(sum) > 1.0e-3 * absolute;'
M 'S21' 'stored zeros connect regions' $TS $F 'if (value != 0.0 && column != row) {' 'if (column != row) {'
M 'S22' 'one anchored row anchors every region' $TS $F 'anchoredRegion[find(row)] = true;' 'std::fill(anchoredRegion.begin(), anchoredRegion.end(), true);'
M 'S23' 'every disconnected model rejected' $TS $F 'if (out.anchoring.unanchored > 0) {' 'if (out.anchoring.regions > 1 || out.anchoring.unanchored > 0) {'
M 'S24' 'missing datum and unanchored region swapped' $TS $F 'if (out.anchoring.anchored == 0) {' 'if (out.anchoring.anchored > 0) {'
M 'S25' 'transient system accepted by a steady solve' $TS $F 'if (system.source().mode != ThermalAnalysisMode::Steady) {' 'if (false) {'
M 'S26' 'numbering of another mesh accepted' $TS $F 'if (!system.describes(map)) {' 'if (false) {'
M 'S27' 'constraint-held postcondition removed from the candidate check' $TS $F "    if (std::optional<SteadySolveFailure> broken = constraintsHeld(constraints, temperatures); broken.has_value()) {`n        return std::unexpected(std::move(*broken));`n    }`n`n    std::vector<double> freeValues" "    std::vector<double> freeValues"
M 'S28' 'candidate finiteness check removed' $TS $F 'if (!std::isfinite(temperatures[i].si())) {' 'if (temperatures.size() > 0xFFFFFFFFFFFFULL) {'
M 'S29' 'kernel refusals reclassified: singular as factorisation failure' $TS $F 'return failure(SteadySolveProblem::SingularSystem, ErrorCode::FailedPrecondition,
                       std::format("the reduced conduction system of {} equations is singular or too ill "' 'return failure(SteadySolveProblem::FactorizationFailure, ErrorCode::FailedPrecondition,
                       std::format("the reduced conduction system of {} equations is singular or too ill "'
M 'S30' 'non-finite reduced right-hand side accepted' $TS $F 'if (!std::isfinite(out.beff_[equation])) {' 'if (out.beff_.size() > 0xFFFFFFFFFFFFULL) {'
M 'L01' 'pivot gate removed from the kernel' $LS $F 'if (!positive || out.solution.pivotRatio < settings.pivotFloor) {' 'if (out.solution.pivotRatio < -1.0) {'
M 'L02' 'residual threshold not applied' $LS $F 'if (!(norms.normalized <= settings.relativeResidualTolerance)) {' 'if (false) {'
M 'L03' 'solution finiteness check removed' $LS $F 'if (!std::isfinite(x[i])) {' 'if (x.size() > 0xFFFFFFFFFFFFULL) {'
M 'L04' 'residual finiteness check removed' $LS $F 'if (!std::isfinite(entry)) {' 'if (rows > 0xFFFFFFFFFFFFULL) {'
M 'L05' 'raw absolute residual used instead of the normalized one' $LS $F 'norms.normalized = euclidean / denominator;' 'norms.normalized = euclidean;'
M 'L06' 'indefinite pivots accepted (positivity check removed)' $LS $F 'if (entry <= 0.0) {' 'if (entry < -1.0e300) {'
M 'L07' 'exact zero pivot reported as a factorisation failure' $LS $F 'const bool zeroPivot = solver.info() == Eigen::NumericalIssue;' 'const bool zeroPivot = false;'
M 'L08' 'right-hand side finiteness not checked' $LS $F 'if (!std::isfinite(rhs[i])) {' 'if (rhs.size() > 0xFFFFFFFFFFFFULL) {'
M 'L09' 'residual evaluation ignores the matrix scale' $LS $F 'const double denominator = matrixInfinity * solutionNorm + rhsNorm;' 'const double denominator = solutionNorm + rhsNorm;'
M 'W01' 'structural wrapper: singular classified as factorisation failure' $WS '[structural]' 'case linalg::SymmetricSolveProblem::SingularSystem:
        return SolveProblem::SingularSystem;' 'case linalg::SymmetricSolveProblem::SingularSystem:
        return SolveProblem::FactorizationFailure;'
M 'W02' 'structural wrapper: the singular message loses the exactly-zero-pivot wording' $WS '[structural]' 'if (failure.exactlyZeroPivot) {' 'if (false) {'
"" | Out-File "$scr\mutation-solve-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-solve-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-solve-progress.txt" -Append
