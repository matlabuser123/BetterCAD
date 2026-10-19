# P18-BC-001 mutation probes, batch 1. Each applies one textual mutation to the
# boundary kernel, the boundary preparation, the target resolution or a canonical
# header, rebuilds the test targets, runs every ThermalBC_* case in its own process
# through ctest (so a mutant that aborts is counted case by case), records the
# verdict and restores the file. Sequential.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$conv = "$root\src\thermal\ThermalConvection.cpp"
$bnd = "$root\src\thermal\ThermalBoundary.cpp"
$tgt = "$root\src\thermal\ThermalTarget.cpp"
$hdr = "$root\include\bettercad\thermal\ThermalData.hpp"
$summary = "$s\mutations-bc-summary.txt"
"P18-BC-001 mutation probes batch 1 started $(Get-Date)" | Out-File $summary
$precheck = $args -contains "-PreCheck"

function Probe($name, $file, $find, $replace) {
    if ($precheck) {
        $text = [IO.File]::ReadAllText($file)
        $n = ([regex]::Matches($text, [regex]::Escape($find))).Count
        "{0,-60} anchor occurrences: {1}" -f $name, $n
        return
    }
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter 'unit\.ThermalBC_' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log" -ErrorAction SilentlyContinue
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception|\(Child aborted" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted or crashed cases $aborted" | Out-File $summary -Append
}

$first = "    Result<ThermalConstraintMap> built = buildThermalConstraintMap(map, wanted);"

# --- the Dirichlet side
Probe "B01-prescribed-temperature-forced-to-zero" $bnd "requests.push_back(TemperatureRequest{.condition = condition.id(), .dof = dof, .value = held->value});" "requests.push_back(TemperatureRequest{.condition = condition.id(), .dof = dof, .value = Temperature::fromSi(0.0)});"
Probe "B02-prescribed-temperature-stored-in-celsius" $bnd "requests.push_back(TemperatureRequest{.condition = condition.id(), .dof = dof, .value = held->value});" "requests.push_back(TemperatureRequest{.condition = condition.id(), .dof = dof, .value = Temperature::fromSi(held->value.si() - 273.15)});"
Probe "B03-node-deduplication-skipped" $bnd "    dofs.erase(last.begin(), last.end());" "    (void)last;"
Probe "B04-first-conflicting-temperature-wins" $bnd $first "    { std::vector<ThermalTemperatureConstraint> kept; for (const auto& w : wanted) { bool seen = false; for (const auto& k : kept) { if (k.dof.value() == w.dof.value()) { seen = true; } } if (!seen) { kept.push_back(w); } } wanted = std::move(kept); }`n$first"
Probe "B05-last-conflicting-temperature-wins" $bnd $first "    { std::vector<ThermalTemperatureConstraint> kept; for (const auto& w : wanted) { bool seen = false; for (auto& k : kept) { if (k.dof.value() == w.dof.value()) { k = w; seen = true; } } if (!seen) { kept.push_back(w); } } wanted = std::move(kept); }`n$first"
Probe "B06-conflicting-temperatures-averaged" $bnd $first "    { std::vector<ThermalTemperatureConstraint> kept; std::vector<int> count; for (const auto& w : wanted) { bool seen = false; for (std::size_t i = 0; i < kept.size(); ++i) { if (kept[i].dof.value() == w.dof.value()) { kept[i].temperature = Temperature::fromSi((kept[i].temperature.si() * count[i] + w.temperature.si()) / (count[i] + 1)); ++count[i]; seen = true; } } if (!seen) { kept.push_back(w); count.push_back(1); } } wanted = std::move(kept); }`n$first"
Probe "B07-condition-attribution-keeps-only-the-first" $bnd "if (std::ranges::find(ids, id) == ids.end()) {" "if (ids.empty()) {"
Probe "B08-conflict-counted-per-request-not-per-unknown" $bnd "} else if (found->second.lastDof != a->dof.value()) {" "} else {"
Probe "B09-empty-dirichlet-target-is-a-success" $bnd "    // for that constrains nothing is a model whose attribution chain lost a face.`n    if (facets.empty()) {" "    // for that constrains nothing is a model whose attribution chain lost a face.`n    if (false) {"
Probe "B10-empty-convection-target-is-a-success" $bnd "    if (facets.empty()) {`n        return std::unexpected(failure(BoundaryProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,`n                                       `"the target resolved to no boundary facet, so there is nothing to `"" "    if (false) {`n        return std::unexpected(failure(BoundaryProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,`n                                       `"the target resolved to no boundary facet, so there is nothing to `""
Probe "B11-facet-uniqueness-checks-removed" $bnd "    if (!strictlyAscending(facets)) {" "    if (false) {"
Probe "B12-numbering-checks-removed" $bnd "    if (!map.describes(mesh)) {" "    if (false) {"
Probe "B13-no-dirichlet-set-rejected" $bnd "    // CONTRADICTIONS LAST, over every prescription of every condition together:" "    if (requests.empty()) { return std::unexpected(failure(BoundaryProblem::TemperatureInvalid, ErrorCode::FailedPrecondition, `"no prescribed temperature`")); }`n    // CONTRADICTIONS LAST, over every prescription of every condition together:"

# --- the Robin kernel
Probe "B14-robin-matrix-centroid-quadrature" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "out.entries_[row * kTri3Nodes + column] = coefficient.si() * (area.si() / 9.0);"
Probe "B15-robin-matrix-diagonally-lumped" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "out.entries_[row * kTri3Nodes + column] = (row == column) ? 4.0 * unit : 0.0;"
Probe "B16-robin-source-omitted" $conv "        out.source_[row] = perNode;" "        out.source_[row] = 0.0;"
Probe "B17-robin-source-negated" $conv "        out.source_[row] = perNode;" "        out.source_[row] = -perNode;"
Probe "B18-robin-matrix-negated" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "out.entries_[row * kTri3Nodes + column] = -((row == column) ? diagonal : off);"
Probe "B19-robin-matrix-multiplied-by-a-conductivity" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "out.entries_[row * kTri3Nodes + column] = 45.0 * ((row == column) ? diagonal : off);"
Probe "B20-robin-matrix-divided-by-the-wrong-factor" $conv "    const double unit = coefficient.si() * (area.si() / 12.0);" "    const double unit = coefficient.si() * (area.si() / 6.0);"
Probe "B21-robin-source-divided-by-the-wrong-factor" $conv "    const double perNode = (coefficient.si() * (area.si() / 3.0)) * ambient.si();" "    const double perNode = (coefficient.si() * (area.si() / 4.0)) * ambient.si();"
Probe "B22-robin-area-in-square-millimetres" $conv "    const double unit = coefficient.si() * (area.si() / 12.0);" "    const double unit = coefficient.si() * ((area.si() * 1.0e6) / 12.0);"
Probe "B23-robin-source-ambient-in-celsius" $conv "    const double perNode = (coefficient.si() * (area.si() / 3.0)) * ambient.si();" "    const double perNode = (coefficient.si() * (area.si() / 3.0)) * (ambient.si() - 273.15);"
Probe "B24-robin-matrix-takes-the-ambient" $conv "    const double unit = coefficient.si() * (area.si() / 12.0);" "    const double unit = coefficient.si() * (area.si() / 12.0) * (1.0 + 1.0e-6 * ambient.si());"
Probe "B25-robin-share-formed-as-product-then-quotient" $conv "    const double perNode = (coefficient.si() * (area.si() / 3.0)) * ambient.si();" "    const double perNode = (coefficient.si() * ambient.si() * area.si()) / 3.0;"
Probe "B26-robin-matrix-overflow-gate-removed" $conv "    for (const double entry : out.entries_) {`n        if (!std::isfinite(entry)) {" "    for (const double entry : out.entries_) {`n        if (false) {"
Probe "B27-robin-source-overflow-gate-removed" $conv "    for (const double entry : out.source_) {`n        if (!std::isfinite(entry)) {" "    for (const double entry : out.source_) {`n        if (false) {"
Probe "B28-negative-coefficient-accepted-in-the-kernel" $conv "    if (!std::isfinite(coefficient.si()) || !(coefficient.si() >= 0.0)) {" "    if (false) {"
Probe "B29-nan-coefficient-passes-the-kernel-check" $conv "    if (!std::isfinite(coefficient.si()) || !(coefficient.si() >= 0.0)) {" "    if (coefficient.si() < 0.0) {"
Probe "B30-ambient-ignored-when-the-coefficient-is-zero" $conv "    if (!isAbsoluteTemperature(ambient)) {" "    if (coefficient.si() != 0.0 && !isAbsoluteTemperature(ambient)) {"
Probe "B31-zero-area-kernel-accepted" $conv "    if (!std::isfinite(area.si()) || !(area.si() > 0.0)) {" "    if (!std::isfinite(area.si())) {"

# --- the boundary preparation
Probe "B32-hand-and-ambient-swapped" $bnd "integrateConvection(mesh, map, *facets, convection->coefficient, convection->ambient);" "integrateConvection(mesh, map, *facets, HeatTransferCoefficient::fromSi(convection->ambient.si()), Temperature::fromSi(convection->coefficient.si()));"
Probe "B33-corner-order-sorted-in-the-facet" $bnd "        out.push_back(ConvectionFacet{.facet = id, .dofs = dofs, .term = std::move(*term)});" "        std::ranges::sort(dofs, [](ThermalDofIndex a, ThermalDofIndex b) { return a.value() < b.value(); });`n        out.push_back(ConvectionFacet{.facet = id, .dofs = dofs, .term = std::move(*term)});"
Probe "B34-zero-area-facet-accepted-by-the-seam" $bnd "        if (!std::isfinite(area.si()) || !(area.si() > 0.0)) {`n            return std::unexpected(failure(BoundaryProblem::DegenerateFacet" "        if (false) {`n            return std::unexpected(failure(BoundaryProblem::DegenerateFacet"
Probe "B35-identical-convections-deduplicated" $bnd "            const ConvectionBoundary* convection = condition.convection();" "            const ConvectionBoundary* convection = condition.convection();`n            if (std::ranges::any_of(convections, [&](const PreparedConvectionBoundary& p) { return p.coefficient.si() == convection->coefficient.si() && p.ambient.si() == convection->ambient.si(); })) { continue; }"
Probe "B36-duplicate-condition-ids-accepted" $bnd "        if (std::ranges::find(seen, condition.id()) != seen.end()) {" "        if (false) {"
Probe "B37-invalid-identity-accepted" $bnd "        if (!condition.id().isValid()) {" "        if (false) {"
Probe "B38-temperature-value-check-removed" $bnd "            if (!isAbsoluteTemperature(held->value)) {" "            if (false) {"
Probe "B39-zero-coefficient-accepted-by-preparation" $bnd "!(convection->coefficient.si() > 0.0)" "(convection->coefficient.si() < 0.0)"
Probe "B40-ambient-value-check-removed" $bnd "            if (!isAbsoluteTemperature(convection->ambient)) {" "            if (false) {"

# --- target resolution (P16 is asked, never answered)
Probe "B41-nearest-face-fallback" $tgt "    if (!set->fullyResolved()) {`n        out.problem = TargetProblem::Unresolved;" "    if (!set->fullyResolved()) {`n        const FaceName nearest{face.feature, FaceSelector{.role = FaceRole::EndCap}};`n        Result<meshing::BoundaryFacetSet> other = meshing::boundaryFacetsOf(map, nearest);`n        if (other.has_value() && other->fullyResolved()) {`n            out.facets = other->facets;`n            return out;`n        }`n        out.problem = TargetProblem::Unresolved;"
Probe "B42-unresolved-treated-as-resolved" $tgt "    if (!set->fullyResolved()) {" "    if (false) {"

# --- provenance
Probe "B43-mesh-stamp-omitted-from-the-source" $bnd "    source.mesh = model.mesh().mesh().stamp();" "    source.mesh = meshing::MeshStamp{};"
Probe "B44-same-counts-taken-as-same-mesh" $bnd "    if (!(prepared.mesh == current.mesh) || prepared.nodeCount != current.nodeCount) {" "    if (prepared.nodeCount != current.nodeCount) {"
Probe "B45-conditions-omitted-from-the-comparison" $bnd "    if (!(prepared.conditions == current.conditions)) {" "    if (false) {"
Probe "B46-body-omitted-from-the-comparison" $bnd "    if (prepared.body != current.body) {" "    if (false) {"
Probe "B47-geometry-omitted-from-the-comparison" $bnd "    if (!(prepared.geometry == current.geometry)) {" "    if (false) {"
Probe "B48-describes-numbering-ignores-the-stamp" $bnd "    return map.stamp() == source_.mesh && map.dofCount() == constraints_->dofCount();" "    return map.dofCount() == constraints_->dofCount();"
Probe "B49-describes-mesh-ignores-the-stamp" $bnd "    return mesh.owns(source_.mesh) && mesh.nodes().size() == source_.nodeCount;" "    return mesh.nodes().size() == source_.nodeCount;"
Probe "B50-material-enters-the-prepared-boundary-identity" $bnd "    source.conditions.assign(conditions.begin(), conditions.end());" "    source.conditions.assign(conditions.begin(), conditions.end());`n    if (model.conductivity().si() != 45.0) {`n        source.conditions.clear();`n    }"
Probe "B51-canonical-prescribed-temperature-persists-a-node" $hdr "    FaceName face{};`n    Temperature value{};`n`n    friend bool operator==(const PrescribedTemperature&, const PrescribedTemperature&) = default;" "    FaceName face{};`n    Temperature value{};`n    std::uint32_t cachedNode = 0;`n`n    friend bool operator==(const PrescribedTemperature&, const PrescribedTemperature&) = default;"
Probe "B52-huge-diagonal-added-to-the-robin-matrix" $conv "    const double diagonal = 2.0 * unit;" "    const double diagonal = 2.0 * unit + 1.0e30;"
"P18-BC-001 mutation probes batch 1 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary