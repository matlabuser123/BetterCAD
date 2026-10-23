# P18-VIZ-001 mutation campaign driver. Each probe edits the named production file, rebuilds bettercad_tests (debug-ext), runs the
# [thermal][viz] cases and restores the file (mutate-t.ps1). The verdict is read from the TEST OUTPUT.
# A literal backslash-n in Find/Replace stands for a newline (the sources are LF).
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$rv = "$root\src\renderer\ThermalResultView.cpp"
$ct = "$root\src\renderer\ThermalContour.cpp"
$in = "$root\src\renderer\ThermalInspection.cpp"
$ov = "$root\src\renderer\ThermalOverlay.cpp"
$oc = "$root\src\renderer\occt\OcctOverlayPresentation.cpp"
$progress = "$s\viz-mutation-progress.txt"
$only = $args
$probes = @(
 # ---- the field mapping and the unit boundary
 @{ Id='VIZ01'; File=$rv; Desc='node values are bound to the nodes in reverse order'; Find='out.nodes.assign(nodes.begin(), nodes.end());'; Replace='out.nodes.assign(nodes.rbegin(), nodes.rend());' },
 @{ Id='VIZ02'; File=$rv; Desc='the temperature channel is converted to Celsius'; Find='return temperature.si();'; Replace='return temperature.si() - 273.15;' },
 @{ Id='VIZ03'; File=$rv; Desc='the temperature channel is rescaled in place by 1000'; Find='return temperature.si();'; Replace='return temperature.si() * 1000.0;' },
 @{ Id='VIZ04'; File=$rv; Desc='the heat-flux magnitude is reported in kW/m^2'; Find='return magnitude.si();'; Replace='return magnitude.si() / 1000.0;' },
 @{ Id='VIZ05'; File=$rv; Desc='the gradient channel shows one component, not the norm'; Find='return std::hypot(gradient.x.si(), gradient.y.si(), gradient.z.si());'; Replace='return gradient.x.si();' },
 @{ Id='VIZ06'; File=$rv; Desc='a NaN gradient component is swallowed by hypot'; Find='if (!std::isfinite(gradient.x.si()) || !std::isfinite(gradient.y.si()) || !std::isfinite(gradient.z.si())) {'; Replace='if (false) {' },
 @{ Id='VIZ07'; File=$rv; Desc='a short temperature array is accepted (zero fill)'; Find='if (stored.temperatures.size() != out.expected) {'; Replace='if (false) {' },
 @{ Id='VIZ08'; File=$rv; Desc='a non-finite value is mapped instead of refused'; Find='if (!std::isfinite(*value)) {'; Replace='if (false) {' },
 @{ Id='VIZ09'; File=$rv; Desc='the lowest value ties resolve to the LAST entity'; Find='if (out.values[i] < out.minimum) {'; Replace='if (out.values[i] <= out.minimum) {' },
 @{ Id='VIZ10'; File=$rv; Desc='the extreme search ties resolve to the LAST entity'; Find='field.values[i] > field.values[at]'; Replace='field.values[i] >= field.values[at]' },
 @{ Id='VIZ11'; File=$rv; Desc='the minimum is seeded from zero instead of the first value'; Find='out.minimum = out.values.front();'; Replace='out.minimum = 0.0;' },
 @{ Id='VIZ12'; File=$rv; Desc='the result elements are not compared id for id with the mesh'; Find='same = ids[i] == tets[i].id;'; Replace='same = true;' },
 @{ Id='VIZ13'; File=$rv; Desc='the result is not checked against the numbering'; Find='if (!post.describes(map)) {'; Replace='if (false) {' },
 @{ Id='VIZ14'; File=$rv; Desc='the view no longer compares the result source'; Find='post.source() == source_ &&'; Replace='true &&' },
 @{ Id='VIZ15'; File=$rv; Desc='a channel with skipped entities is still a field'; Find='if (out.skipped > 0U) {'; Replace='if (false) {' },
 @{ Id='VIZ16'; File=$rv; Desc='an extreme over a partial field is allowed'; Find='if (!field.complete() || field.values.empty() || field.values.size() != field.classified) {'; Replace='if (false) {' },
 @{ Id='VIZ17'; File=$oc; Desc='the OCCT adapter applies the metre-to-millimetre conversion twice'; Find='toModelLength(segment.from.x.si())'; Replace='toModelLength(toModelLength(segment.from.x.si()))' },
 @{ Id='VIZ18'; File=$oc; Desc='the OCCT adapter applies no conversion (metres drawn as millimetres)'; Find='return geometry::occt::toModel(Length::fromSi(metres));'; Replace='return metres;' },
 # ---- the contour
 @{ Id='VIZ19'; File=$ct; Desc='a manual range stops clamping the colour (the value is refused)'; Find='return value < bands.front().lower ? 0U : bands.size() - 1U;'; Replace='return bands.size();' },
 @{ Id='VIZ20'; File=$ct; Desc='the last band is half open (the maximum falls in no band)'; Find='band.closedAbove = (i + 1U == bands);'; Replace='band.closedAbove = false;' },
 @{ Id='VIZ21'; File=$ct; Desc='a uniform range is divided (0/0) instead of one band'; Find='if (span == 0.0 || out.minimum + width == out.minimum) {'; Replace='if (false) {' },
 @{ Id='VIZ22'; File=$ct; Desc='the shared band boundaries are not clamped against their predecessor'; Find='boundary[i] = std::clamp(computed, boundary[i - 1U], out.maximum);'; Replace='boundary[i] = computed;' },
 @{ Id='VIZ23'; File=$ct; Desc='a legend of another field is accepted'; Find='if (legend.channel != field.channel || legend.fieldMinimum != field.minimum || legend.fieldMaximum != field.maximum) {'; Replace='if (false) {' },
 @{ Id='VIZ24'; File=$ct; Desc='a partial field is banded'; Find='    // presents the entities that happened to classify as if they were the result.\n    if (!field.complete()) {'; Replace='    // presents the entities that happened to classify as if they were the result.\n    if (false) {' },
 @{ Id='VIZ25'; File=$ct; Desc='an element channel is drawn on the boundary-triangle view'; Find='if (!nodal && view.source() != MeshSource::VolumeBoundaryByTetrahedron &&'; Replace='if (false && view.source() != MeshSource::VolumeBoundaryByTetrahedron &&' },
 @{ Id='VIZ26'; File=$ct; Desc='an element value is smoothed: the third corner takes a neighbour element band'; Find='out.push_back(entityBands[at]);\n            out.push_back(entityBands[at]);\n            out.push_back(entityBands[at]);'; Replace='out.push_back(entityBands[at]);\n            out.push_back(entityBands[at]);\n            out.push_back(entityBands[(at + 1U) % entityBands.size()]);' },
 @{ Id='VIZ27'; File=$ct; Desc='all three corners of a nodal triangle take the first vertex'; Find='const std::size_t vertex = indices[(triangle * 3U) + corner];'; Replace='const std::size_t vertex = indices[(triangle * 3U)];' },
 @{ Id='VIZ28'; File=$ct; Desc='the cache ignores the displayed frame'; Find='wanted.frame = frame;'; Replace='wanted.frame = 0;' },
 @{ Id='VIZ29'; File=$ct; Desc='the cache ignores the result source'; Find='wanted.source = post.source();'; Replace='wanted.source = {};' },
 @{ Id='VIZ30'; File=$ct; Desc='the cache ignores the display range'; Find='wanted.range = range;'; Replace='wanted.range = std::nullopt;' },
 @{ Id='VIZ31'; File=$ct; Desc='the cache ignores the view it was built for'; Find='if (key_.has_value() && *key_ == wanted && view.describes(mesh)) {'; Replace='if (key_.has_value() && key_->source == wanted.source && key_->mesh == wanted.mesh && key_->frame == wanted.frame && key_->channel == wanted.channel && key_->bands == wanted.bands && key_->range == wanted.range) {' },
 @{ Id='VIZ32'; File=$ct; Desc='a failed rebuild leaves the held buffer as the answer'; Find='    clear();\n    const Result<ThermalResultView> result = ThermalResultView::of(mesh, view, map, post);'; Replace='    const Result<ThermalResultView> result = ThermalResultView::of(mesh, view, map, post);' },
 # ---- the display state, the badge, the availability
 @{ Id='VIZ33'; File=$in; Desc='any held result is current'; Find='if (isCurrentFor(document, *model, definition, *post)) {'; Replace='if (isCurrentFor(document, *model, definition, *post) || true) {' },
 @{ Id='VIZ34'; File=$in; Desc='a failed recompute brings the old result back as current'; Find='out.result = lastFailure != nullptr ? ThermalResultState::SolveFailed : ThermalResultState::Stale;'; Replace='out.result = ThermalResultState::Current;' },
 @{ Id='VIZ35'; File=$in; Desc='the stale reasons are not reported'; Find='out.staleReasons = reasonsFor(document, *model, definition, *post);'; Replace='out.staleReasons = reasonsFor(document, *model, definition, *post);\n    out.staleReasons.clear();' },
 @{ Id='VIZ36'; File=$in; Desc='a stale report keeps its badge'; Find='if (!otherResult && thermal::isCurrent(*report, document, regenerator, mesher)) {'; Replace='if (!otherResult) {' },
 @{ Id='VIZ37'; File=$in; Desc='the GUI downgrades a failed validation to a pass-with-warnings badge'; Find='out.badge = ThermalValidationBadge::Fail;\n                break;'; Replace='out.badge = ThermalValidationBadge::PassWithWarnings;\n                break;' },
 @{ Id='VIZ38'; File=$in; Desc='the GUI hides warnings: pass-with-warnings is shown as pass'; Find='out.badge = ThermalValidationBadge::PassWithWarnings;'; Replace='out.badge = ThermalValidationBadge::Pass;' },
 @{ Id='VIZ39'; File=$in; Desc='a failed validation no longer makes the fields unavailable'; Find='return state.badge == ThermalValidationBadge::Fail ? ThermalFieldAvailability::InvalidResult\n                                                           : ThermalFieldAvailability::Available;'; Replace='return ThermalFieldAvailability::Available;' },
 @{ Id='VIZ40'; File=$in; Desc='a stale result leaves the fields available'; Find='case ThermalResultState::Stale:\n        return ThermalFieldAvailability::StaleResult;'; Replace='case ThermalResultState::Stale:\n        return ThermalFieldAvailability::Available;' },
 @{ Id='VIZ41'; File=$in; Desc='the stale banner is blank'; Find='return "STALE RESULT - recompute required";'; Replace='return "";' },
 @{ Id='VIZ42'; File=$in; Desc='a report of another analysis supplies the badge'; Find='if (report != nullptr && report->analysis.has_value() && *report->analysis == analysis) {'; Replace='if (report != nullptr) {' },
 @{ Id='VIZ43'; File=$in; Desc='a report about another result keeps its badge'; Find='const bool otherResult = post != nullptr && report->resultSource.has_value() && !(*report->resultSource == post->source());'; Replace='const bool otherResult = false;' },
 # ---- inspection, navigation, playback
 @{ Id='VIZ44'; File=$in; Desc='a node is inspected through its raw id as an index'; Find='out.temperature = *temperature;'; Replace='out.temperature = post.states()[state].temperatures[static_cast<std::size_t>(node.value()) % post.states()[state].temperatures.size()];' },
 @{ Id='VIZ45'; File=$in; Desc='an element ordinal is its raw id'; Find='out.ordinal = *ordinal;'; Replace='out.ordinal = static_cast<std::size_t>(element.value());' },
 @{ Id='VIZ46'; File=$in; Desc='the gradient magnitude is one component'; Find='out.gradientMagnitude = TemperatureGradient::fromSi(gradientDisplayNorm(*gradient));'; Replace='out.gradientMagnitude = TemperatureGradient::fromSi(gradient->x.si());' },
 @{ Id='VIZ47'; File=$in; Desc='the inspected heat flux is sign-flipped'; Find='out.heatFlux = *flux;'; Replace='thermal::HeatFlux3 flipped = *flux;\n    flipped.x = HeatFlux::fromSi(-flipped.x.si());\n    flipped.y = HeatFlux::fromSi(-flipped.y.si());\n    flipped.z = HeatFlux::fromSi(-flipped.z.si());\n    out.heatFlux = flipped;' },
 @{ Id='VIZ48'; File=$in; Desc='a node of another numbering is inspected'; Find='if (!map.describes(mesh) || !post.describes(map)) {'; Replace='if (false) {' },
 @{ Id='VIZ49'; File=$in; Desc='an element of another mesh generation is inspected'; Find='if (!mesh.owns(post.source().system.mesh)) {'; Replace='if (false) {' },
 @{ Id='VIZ50'; File=$in; Desc='the navigation target of an element is not its centroid'; Find='Length::fromSi(x * 0.25), Length::fromSi(y * 0.25), Length::fromSi(z * 0.25)'; Replace='Length::fromSi(x * 0.5), Length::fromSi(y * 0.5), Length::fromSi(z * 0.5)' },
 @{ Id='VIZ51'; File=$in; Desc='a frame time is the frame index times a nominal step'; Find='out.times_.push_back(t->si());'; Replace='out.times_.push_back(static_cast<double>(i) * 0.3);' },
 @{ Id='VIZ52'; File=$in; Desc='non-increasing stored times are accepted'; Find='if (!out.times_.empty() && !(t->si() > out.times_.back())) {'; Replace='if (false) {' },
 @{ Id='VIZ53'; File=$in; Desc='a steady result reports a time'; Find='if (!transient_ || frame >= times_.size()) {'; Replace='if (frame >= times_.size()) {' },
 @{ Id='VIZ54'; File=$in; Desc='a frame outside the stored states can be selected'; Find='if (frame >= times_.size()) {\n        return makeError(ErrorCode::InvalidArgument,\n                         std::format("frame {} requested'; Replace='if (false) {\n        return makeError(ErrorCode::InvalidArgument,\n                         std::format("frame {} requested' },
 # ---- vectors, arrows, symbols, cardinality
 @{ Id='VIZ55'; File=$ov; Desc='the outward normal is inward (winding reversed)'; Find='const V3 area = crossOf(minus((*corners)[1], (*corners)[0]), minus((*corners)[2], (*corners)[0]));'; Replace='const V3 area = crossOf(minus((*corners)[2], (*corners)[0]), minus((*corners)[1], (*corners)[0]));' },
 @{ Id='VIZ56'; File=$ov; Desc='a positive applied flux points out of the body'; Find='const double along = glyph.flux.si() > 0.0 ? -1.0 : 1.0;'; Replace='const double along = glyph.flux.si() > 0.0 ? 1.0 : -1.0;' },
 @{ Id='VIZ57'; File=$ov; Desc='the recovered heat-flux direction is negated'; Find='glyph.direction = Vector3D{flux->x.si() / m, flux->y.si() / m, flux->z.si() / m};'; Replace='glyph.direction = Vector3D{-flux->x.si() / m, -flux->y.si() / m, -flux->z.si() / m};' },
 @{ Id='VIZ58'; File=$ov; Desc='the stored heat-flux vector is sign-flipped in the glyph'; Find='glyph.flux = *flux;'; Replace='thermal::HeatFlux3 flipped = *flux;\n        flipped.x = HeatFlux::fromSi(-flipped.x.si());\n        flipped.y = HeatFlux::fromSi(-flipped.y.si());\n        flipped.z = HeatFlux::fromSi(-flipped.z.si());\n        glyph.flux = flipped;' },
 @{ Id='VIZ59'; File=$ov; Desc='a zero heat flux is given an invented direction'; Find='if (m > 0.0 && std::isfinite(m)) {'; Replace='if (std::isfinite(m)) {' },
 @{ Id='VIZ60'; File=$ov; Desc='a zero applied flux gets an arrow'; Find='glyph.hasDirection = std::isfinite(surface->flux.si()) && surface->flux.si() != 0.0;'; Replace='glyph.hasDirection = true;' },
 @{ Id='VIZ61'; File=$ov; Desc='a zero-classified target reads as complete'; Find='} else if (out.expected == 0U || classified == 0U) {'; Replace='} else if (out.expected == 0U) {' },
 @{ Id='VIZ62'; File=$ov; Desc='a partial classification reads as complete'; Find='} else if (classified < out.expected || rendered < out.expected) {'; Replace='} else if (false) {' },
 @{ Id='VIZ63'; File=$ov; Desc='an unresolved mapping is classified'; Find='if (!mapping.fullyResolved()) {'; Replace='if (false) {' },
 @{ Id='VIZ64'; File=$ov; Desc='the rendered count is not part of completeness'; Find='} else if (classified < out.expected || rendered < out.expected) {'; Replace='} else if (classified < out.expected) {' },
 @{ Id='VIZ65'; File=$ov; Desc='the applied-flux magnitude is signed (a negative flux is refused)'; Find='thermalDisplayArrowLength(std::abs(glyph.flux.si()), reference, scale)'; Replace='thermalDisplayArrowLength(glyph.flux.si(), reference, scale)' },
 @{ Id='VIZ66'; File=$ov; Desc='identical prescriptions are drawn twice'; Find='if (!drawn.emplace(facet.value(), glyph.value.si()).second) {'; Replace='if (false) {' },
 @{ Id='VIZ67'; File=$ov; Desc='the display threshold is ignored'; Find='if (glyph.magnitude.si() / reference < minimumFraction) {'; Replace='if (false) {' },
 @{ Id='VIZ68'; File=$ov; Desc='the recovered arrow is drawn with its head at the centroid'; Find='addArrow(out.segments, plus(tail, times(direction, *length)), direction, *length);'; Replace='addArrow(out.segments, tail, direction, *length);' },
 @{ Id='VIZ69'; File=$ov; Desc='the named-set cardinality counts every boundary triangle'; Find='if (source.has_value() && std::ranges::binary_search(faces, source->face)) {'; Replace='if (true) {' },
 @{ Id='VIZ70'; File=$ov; Desc='an overflowing arrow length reaches the geometry'; Find='if (!std::isfinite(length)) {'; Replace='if (false) {' },
 @{ Id='VIZ71'; File=$ov; Desc='loads other than the prepared ones are drawn'; Find='if (!std::ranges::equal(prepared.source().loads, loads)) {'; Replace='if (false) {' },
 @{ Id='VIZ72'; File=$ov; Desc='conditions other than the prepared ones are drawn'; Find='if (!std::ranges::equal(prepared.source().conditions, conditions)) {'; Replace='if (false) {' },
 @{ Id='VIZ73'; File=$ov; Desc='an incomplete target is not counted by the overlay'; Find='++out.incompleteTargets;'; Replace='(void)0;' },
 @{ Id='VIZ74'; File=$ov; Desc='a vector glyph takes its ordinal from the raw element id'; Find='glyph.ordinal = *ordinal;'; Replace='glyph.ordinal = static_cast<std::size_t>(element.value());' },
 @{ Id='VIZ75'; File=$ov; Desc='a result of another mesh generation is drawn'; Find='if (!mesh.owns(post.source().system.mesh)) {'; Replace='if (false) {' }
)
if ($only.Count -gt 0) { $probes = $probes | Where-Object { $only -contains $_.Id } }
foreach ($p in $probes) {
    $find = $p.Find.Replace('\n', "`n")
    $replace = $p.Replace.Replace('\n', "`n")
    if ($env:VIZ_DRYRUN) {
        $t = [IO.File]::ReadAllText($p.File)
        $n = ([regex]::Matches($t, [regex]::Escape($find))).Count
        "{0} occurrences={1}  {2}" -f $p.Id, $n, $p.Desc
        continue
    }
    $before = [IO.File]::ReadAllText($p.File)
    $started = Get-Date
    [IO.File]::WriteAllText("$s\find-$($p.Id).txt", $find, (New-Object Text.UTF8Encoding $false))
    [IO.File]::WriteAllText("$s\replace-$($p.Id).txt", $replace, (New-Object Text.UTF8Encoding $false))
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-t.ps1" -Name $p.Id -File $p.File -FindFile "$s\find-$($p.Id).txt" -ReplaceFile "$s\replace-$($p.Id).txt" -Filter "[thermal][viz]" -TimeoutSeconds 600 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log" -Raw
    $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } elseif ($log -match 'BUILD FAILED') { 'VOID (did not compile)' } elseif ($log -match 'was not found') { 'NOT APPLIED (find text missing)' } else { 'UNKNOWN' }
    $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
    $after = [IO.File]::ReadAllText($p.File)
    $restored = if ($before -ceq $after) { 'restored' } else { 'NOT RESTORED' }
    $line = "{0} | {1} | {2} | {3} | {4} | {5:N0}s" -f $p.Id, $p.Desc, $verdict, $tests, $restored, ((Get-Date) - $started).TotalSeconds
    Add-Content -Path $progress -Value $line -Encoding utf8
}
Add-Content -Path $progress -Value "campaign finished $(Get-Date -Format s)" -Encoding utf8
