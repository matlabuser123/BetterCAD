# P18-LOAD-001 mutation probes. Each applies one textual mutation to the load
# kernel or the target resolution, rebuilds the test targets, runs every
# ThermalLoad_* case in its own process through ctest (so a mutant that aborts is
# counted case by case), records the verdict and restores the file. Sequential.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalLoadVector.cpp"
$tgt = "$root\src\thermal\ThermalTarget.cpp"
$summary = "$s\mutations-load-summary.txt"
"P18-LOAD-001 mutation probes started $(Get-Date)" | Out-File $summary

function Probe($name, $file, $find, $replace) {
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter 'unit\.ThermalLoad_' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log" -ErrorAction SilentlyContinue
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted cases $aborted" | Out-File $summary -Append
}

# --- the two integrals
Probe "L01-surface-share-without-the-third" $cpp "    return Power::fromSi(flux.si() * area.si() / 3.0);" "    return Power::fromSi(flux.si() * area.si());"
Probe "L02-surface-share-over-four" $cpp "    return Power::fromSi(flux.si() * area.si() / 3.0);" "    return Power::fromSi(flux.si() * area.si() / 4.0);"
Probe "L03-volume-share-over-three" $cpp "    return Power::fromSi(generation.si() * volume.si() / 4.0);" "    return Power::fromSi(generation.si() * volume.si() / 3.0);"
Probe "L04-volume-share-without-the-quarter" $cpp "    return Power::fromSi(generation.si() * volume.si() / 4.0);" "    return Power::fromSi(generation.si() * volume.si());"
Probe "L05-facet-area-doubled" $cpp "    const double share = facetNodalHeat(Area::fromSi(facetArea), flux).si();" "    const double share = facetNodalHeat(Area::fromSi(2.0 * facetArea), flux).si();"
# --- sign and units
Probe "L06-flux-sign-flipped" $cpp "    return Power::fromSi(flux.si() * area.si() / 3.0);" "    return Power::fromSi(-flux.si() * area.si() / 3.0);"
Probe "L07-absolute-heat-flux" $cpp "    return Power::fromSi(flux.si() * area.si() / 3.0);" "    return Power::fromSi(std::abs(flux.si()) * area.si() / 3.0);"
Probe "L08-absolute-generation" $cpp "    return Power::fromSi(generation.si() * volume.si() / 4.0);" "    return Power::fromSi(std::abs(generation.si()) * volume.si() / 4.0);"
Probe "L09-area-in-square-millimetres" $cpp "    return Power::fromSi(flux.si() * area.si() / 3.0);" "    return Power::fromSi(flux.si() * (area.si() * 1.0e6) / 3.0);"
Probe "L10-volume-in-cubic-millimetres" $cpp "    return Power::fromSi(generation.si() * volume.si() / 4.0);" "    return Power::fromSi(generation.si() * (volume.si() * 1.0e9) / 4.0);"
# --- accumulation and indexing
Probe "L11-shared-node-overwritten-not-summed" $cpp "    entry = Power::fromSi(entry.si() + share);" "    entry = Power::fromSi(share);"
Probe "L12-vector-indexed-by-node-id-value" $cpp "    return map.dofOf(node);" "    return ThermalDofIndex::fromValue(node.value());"
Probe "L13-vector-sized-by-max-node-id" $cpp "    out.heatInput.assign(static_cast<std::size_t>(map.dofCount()), Power{});" "    out.heatInput.assign(static_cast<std::size_t>(mesh.nodes().back().id.value()) + 1, Power{});"
Probe "L14-numbering-check-removed" $cpp "    if (!map.describes(mesh)) {" "    if (false) {"
# --- entity sets
Probe "L15-one-boundary-facet-dropped" $cpp "    for (const meshing::ElementId id : facets) {" "    for (const meshing::ElementId id : (facets.size() > 1 ? facets.subspan(1) : facets)) {"
Probe "L16-one-tetrahedron-dropped" $cpp "    for (const meshing::ElementId id : elements) {" "    for (const meshing::ElementId id : (elements.size() > 1 ? elements.subspan(1) : elements)) {"
Probe "L17-facet-uniqueness-check-removed" $cpp "    if (!strictlyAscending(facets)) {" "    if (false) {"
Probe "L18-element-uniqueness-check-removed" $cpp "    if (!strictlyAscending(elements)) {" "    if (false) {"
Probe "L19-equal-ids-accepted-as-ascending" $cpp "return !(a < b); }) ==" "return b < a; }) =="
Probe "L20-empty-facet-list-is-a-zero-load" $cpp "    if (facets.empty()) {" "    if (false) {"
Probe "L21-empty-element-list-is-a-zero-load" $cpp "    if (elements.empty()) {" "    if (false) {"
# --- validity gates
Probe "L22-zero-area-facet-accepted" $cpp "        if (!std::isfinite(facetArea) || !(facetArea > 0.0)) {" "        if (false) {"
Probe "L23-element-validity-gate-removed" $cpp "        if (!geometry.has_value()) {" "        if (false) {"
Probe "L24-missing-triangle-not-detected" $cpp "        if (triangle == nullptr) {" "        if (false) {"
Probe "L25-duplicate-load-id-accepted" $cpp "        if (std::ranges::find(seen, load.id()) != seen.end()) {" "        if (false) {"
Probe "L26-non-finite-value-accepted" $cpp "        if (!valueIsFinite(load)) {" "        if (false) {"
Probe "L27-final-finiteness-gate-removed" $cpp "        if (!std::isfinite(out.heatInput[index].si())) {" "        if (false) {"
Probe "L28-extra-source-term-in-the-load-vector" $cpp "    // EVERY ENTRY FINITE." "    if (!out.heatInput.empty()) {`n        out.heatInput[0] = Power::fromSi(out.heatInput[0].si() + 1.0);`n    }`n    // EVERY ENTRY FINITE."
# --- currentness
Probe "L29-mesh-stamp-omitted-from-the-source" $cpp "    source.mesh = model.mesh().mesh().stamp();" "    source.mesh = meshing::MeshStamp{};"
Probe "L30-same-counts-taken-as-same-mesh" $cpp "    if (!(prepared.mesh == current.mesh) || prepared.nodeCount != current.nodeCount) {" "    if (prepared.nodeCount != current.nodeCount) {"
Probe "L31-loads-omitted-from-the-comparison" $cpp "    if (!(prepared.loads == current.loads)) {" "    if (false) {"
Probe "L32-body-omitted-from-the-comparison" $cpp "    if (prepared.body != current.body) {" "    if (false) {"
Probe "L33-geometry-omitted-from-the-comparison" $cpp "    if (!(prepared.geometry == current.geometry)) {" "    if (false) {"
Probe "L34-describes-numbering-ignores-the-stamp" $cpp "    return map.stamp() == source_.mesh &&" "    return true &&"
Probe "L35-describes-mesh-ignores-the-stamp" $cpp "    return mesh.owns(source_.mesh) && mesh.nodes().size() == source_.nodeCount;" "    return mesh.nodes().size() == source_.nodeCount;"
# --- attribution
Probe "L36-surface-total-includes-the-volume" $cpp "        if (contribution.kind == ThermalLoadKind::SurfaceHeatFlux) {" "        if (true) {"
# --- target resolution (P16 is asked, never answered)
Probe "L37-nearest-face-fallback" $tgt "    if (!set->fullyResolved()) {`n        out.problem = TargetProblem::Unresolved;" "    if (!set->fullyResolved()) {`n        const FaceName nearest{face.feature, FaceSelector{.role = FaceRole::EndCap}};`n        Result<meshing::BoundaryFacetSet> other = meshing::boundaryFacetsOf(map, nearest);`n        if (other.has_value() && other->fullyResolved()) {`n            out.facets = other->facets;`n            return out;`n        }`n        out.problem = TargetProblem::Unresolved;"
Probe "L38-unresolved-reported-as-without-facets" $tgt "        out.problem = TargetProblem::Unresolved;" "        out.problem = TargetProblem::WithoutFacets;"
Probe "L39-unresolved-treated-as-resolved" $tgt "    if (!set->fullyResolved()) {" "    if (false) {"

# --- the abs(V) mutant needs the validity gate AND the volume together, so the
#     find string is cut from the file itself rather than retyped.
$text = [IO.File]::ReadAllText($cpp)
$a = $text.IndexOf("        const Result<Tet4Geometry> geometry = computeTet4Geometry(*corners);")
$bMarker = "        const double share = elementNodalHeat(geometry->volume(), generation).si();"
$b = $text.IndexOf($bMarker)
if ($a -ge 0 -and $b -gt $a) {
    $find = $text.Substring($a, $b + $bMarker.Length - $a)
    $replace = @"
        const double rawVolume = meshing::signedVolume((*corners)[0], (*corners)[1], (*corners)[2], (*corners)[3]).si();
        std::array<ThermalDofIndex, 4> dofs{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
                                            ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0)};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const Result<ThermalDofIndex> dof = dofOfNode(map, tet->nodes[corner]);
            if (!dof.has_value()) {
                return std::unexpected(LoadFailure{.problem = LoadProblem::EntityNotInMesh, .error = dof.error()});
            }
            dofs[corner] = *dof;
        }
        const double elementVolume = std::abs(rawVolume);
        const double share = elementNodalHeat(Volume::fromSi(elementVolume), generation).si();
"@
    $replace = $replace -replace "`r`n", "`n"
    Probe "L40-absolute-tetrahedron-volume-without-the-gate" $cpp ($find -replace "`r`n", "`n") $replace
} else {
    "L40-absolute-tetrahedron-volume-without-the-gate : VOID : markers not found" | Out-File $summary -Append
}
"P18-LOAD-001 mutation probes finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
