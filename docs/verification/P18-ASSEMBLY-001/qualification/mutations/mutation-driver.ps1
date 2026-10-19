$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$SYS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSystem.cpp"
$SPR = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSparse.cpp"
$m = @()
function M($n,$desc,$f,$find,$rep){ $script:m += ,@($n,$desc,$f,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
M 'A01' 'conduction contribution overwrites instead of summing' $SYS 'conductanceValues[*slot] += (*ke)(a, b).si();' 'conductanceValues[*slot] = (*ke)(a, b).si();'
M 'A02' 'capacity contribution overwrites instead of summing' $SYS 'capacityValues[*slot] += (*ce)(a, b).si();' 'capacityValues[*slot] = (*ce)(a, b).si();'
M 'A03' 'prepared heat input omitted' $SYS 'heat[row] += data.heatInput[row].si();' 'heat[row] += 0.0 * data.heatInput[row].si();'
M 'A04' 'convection source overwrites instead of summing' $SYS 'heat[row] += facet.term.source(a).si();' 'heat[row] = facet.term.source(a).si();'
M 'A05' 'convection source omitted' $SYS 'heat[row] += facet.term.source(a).si();' 'heat[row] += 0.0 * facet.term.source(a).si();'
M 'A06' 'convection matrix omitted' $SYS 'conductanceValues[*slot] += facet.term(a, b).si();' 'conductanceValues[*slot] += 0.0 * facet.term(a, b).si();'
M 'A07' 'convection facet rows rotated' $SYS 'const auto row = static_cast<std::size_t>(facet.dofs[a].value());' 'const auto row = static_cast<std::size_t>(facet.dofs[(a + 1) % kTri3Nodes].value());'
M 'A08' 'element columns reversed against rows' $SYS "const auto column = static_cast<std::size_t>((*dofs)[b].value());`n                const std::optional<std::size_t> slot = slotOf(conductanceLayout, row, column);" "const auto column = static_cast<std::size_t>((*dofs)[3 - b].value());`n                const std::optional<std::size_t> slot = slotOf(conductanceLayout, row, column);"
M 'A09' 'node handle minus one used as the row' $SYS "{`n            const auto row = static_cast<std::size_t>((*dofs)[a].value());" "{`n            const auto row = (static_cast<std::size_t>(tet.nodes[a].value()) - 1) % rows;"
M 'A10' 'convection facets not reserved in the K pattern' $SYS 'for (const ThermalDofIndex column : facet.dofs) {' 'for (const ThermalDofIndex column : std::span<const ThermalDofIndex>{}) {'
M 'A11' 'duplicate columns kept in the layout' $SYS 'columns.erase(std::ranges::unique(columns).begin(), columns.end());' '(void)columns;'
M 'A12' 'row columns not sorted' $SYS 'std::ranges::sort(columns);' '(void)columns;'
M 'A13' 'only the upper triangle of Ke scattered' $SYS 'conductanceValues[*slot] += (*ke)(a, b).si();' 'if (b >= a) { conductanceValues[*slot] += (*ke)(a, b).si(); }'
M 'A14' 'element order not recorded' $SYS 'elementOrder.push_back(tet.id);' '(void)tet;'
M 'A15' 'steady always (no capacity ever)' $SYS 'const bool transient = data.capacity.has_value();' 'const bool transient = false;'
M 'A16' 'recorded mode always steady' $SYS 'source.mode = transient ? ThermalAnalysisMode::Transient : ThermalAnalysisMode::Steady;' 'source.mode = ThermalAnalysisMode::Steady;'
M 'A17' 'mesh stamp not recorded' $SYS 'source.mesh = map.stamp();' 'source.mesh = meshing::MeshStamp{};'
M 'A18' 'node count not recorded' $SYS 'source.nodeCount = mesh.nodes().size();' 'source.nodeCount = 0;'
M 'A19' 'body not compared' $SYS 'if (assembled.body != current.body) {' 'if (assembled.body != current.body && current.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'A20' 'mesh stamp not compared' $SYS '!(assembled.mesh == current.mesh) || ' ''
M 'A21' 'node count not compared' $SYS ' || assembled.nodeCount != current.nodeCount' ''
M 'A22' 'geometry not compared' $SYS 'if (!(assembled.geometry == current.geometry)) {' 'if (!(assembled.geometry == current.geometry) && current.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'A23' 'mode not compared' $SYS 'if (assembled.mode != current.mode) {' 'if (assembled.mode != current.mode && current.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'A24' 'material revision not compared' $SYS ' || assembled.materialRevision != current.materialRevision' ''
M 'A25' 'material id not compared' $SYS 'assembled.material != current.material || ' ''
M 'A26' 'loads not compared' $SYS 'if (!(assembled.loads == current.loads)) {' 'if (!(assembled.loads == current.loads) && current.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'A27' 'convections not compared' $SYS 'if (!(assembled.convections == current.convections)) {' 'if (!(assembled.convections == current.convections) && current.nodeCount == 0xFFFFFFFFFFFFULL) {'
M 'A28' 'prescribed temperatures enter the source' $SYS 'if (condition.convection() != nullptr) {' 'if (condition.convection() != nullptr || condition.convection() == nullptr) {'
M 'A29' 'loads not recorded in the source' $SYS 'source.loads.assign(loads.begin(), loads.end());' '(void)loads;'
M 'A30' 'material revision not recorded' $SYS 'source.materialRevision = material.revision();' 'source.materialRevision = 0;'
M 'A31' 'geometry not recorded' $SYS 'source.geometry = model.geometryRevision();' 'source.geometry = meshing::GeometryRevision{};'
M 'A32' 'body not recorded' $SYS 'source.body = model.body();' 'source.body = ObjectId{};'
M 'A33' 'model-level numbering check removed' $SYS "never as whatever numerical symptom it would have caused.`n    if (!map.describes(mesh)) {" "never as whatever numerical symptom it would have caused.`n    if (map.dofCount() > 0xFFFFFFFFFFFFULL) {"
M 'A34' 'prepared load staleness not checked' $SYS "staleReasons(preparedLoads.source(), currentLoadSource(model, loads));`n        !stale.empty()) {" "staleReasons(preparedLoads.source(), currentLoadSource(model, loads));`n        !stale.empty() && stale.size() > 1000) {"
M 'A35' 'prepared boundary staleness not checked' $SYS "staleReasons(preparedBoundary.source(), currentBoundarySource(model, conditions));`n        !stale.empty()) {" "staleReasons(preparedBoundary.source(), currentBoundarySource(model, conditions));`n        !stale.empty() && stale.size() > 1000) {"
M 'A36' 'conductivity edit not detected' $SYS 'if (!(material->conductivity() == model.conductivity())) {' 'if (material->conductivity().si() < 0.0) {'
M 'A37' 'transient capacity never requested' $SYS '.capacity = material->capacity(),' '.capacity = std::nullopt,'
M 'A38' 'convection dof range check removed' $SYS 'if (dof.value() >= map.dofCount()) {' 'if (dof.value() > map.dofCount() + 1000000) {'
M 'A39' 'heat vector length check removed' $SYS 'if (data.heatInput.size() != rows) {' 'if (data.heatInput.size() > rows + 100000) {'
M 'A40' 'empty mesh accepted' $SYS 'if (tets.empty()) {' 'if (tets.size() > 0xFFFFFFFFFFFFULL) {'
M 'A41' 'rejected element skipped' $SYS 'if (!geometry.has_value()) {' "if (!geometry.has_value()) {`n            continue;`n        }`n        if (!geometry.has_value() && rows > 0xFFFFFFFFFFFFULL) {"
M 'A42' 'convection matrix negated' $SYS 'conductanceValues[*slot] += facet.term(a, b).si();' 'conductanceValues[*slot] -= facet.term(a, b).si();'
M 'A43' 'convection source negated' $SYS 'heat[row] += facet.term.source(a).si();' 'heat[row] -= facet.term.source(a).si();'
M 'A44' 'capacity halved' $SYS 'capacityValues[*slot] += (*ce)(a, b).si();' 'capacityValues[*slot] += 0.5 * (*ce)(a, b).si();'
M 'A45' 'diagonal epsilon added to K' $SYS 'conductanceValues[*slot] += (*ke)(a, b).si();' 'conductanceValues[*slot] += (*ke)(a, b).si() + (a == b ? 1.0e-12 : 0.0);'
M 'A46' 'elements visited in reverse order' $SYS "elementOrder.reserve(tets.size());`n    for (const meshing::Tetrahedron& tet : tets) {" "elementOrder.reserve(tets.size());`n    for (const meshing::Tetrahedron& tet : std::ranges::reverse_view{tets}) {"
M 'A47' 'non-finite classified as inconsistent' $SYS 'refused.problem == MatrixProblem::NonFiniteEntry' 'refused.problem == MatrixProblem::NotSymmetric'
M 'A48' 'wrong conductivity (doubled) passed to the kernel' $SYS 'computeTet4Conduction(*geometry, data.conductivity)' 'computeTet4Conduction(*geometry, ThermalConductivity::fromSi(2.0 * data.conductivity.si()))'
M 'A49' 'capacity computed with density ignored' $SYS 'computeTet4Capacity(*geometry, *data.capacity)' 'computeTet4Capacity(*geometry, ThermalCapacity{.density = Density::fromSi(1.0), .specificHeatCapacity = data.capacity->specificHeatCapacity})'
M 'A50' 'lookup returns the neighbour for an absent pair' $SPR '    if (found == last || *found != static_cast<Index>(column)) {
        return 0.0;
    }
    return values_[' '    if (found == last) {
        return 0.0;
    }
    return values_['
M 'A51' 'symmetry error always zero' $SPR 'largest = std::max(largest, std::abs(values_[slot] - coeffSi(column, row)));' '(void)column; largest = std::max(largest, std::abs(values_[slot] - values_[slot]));'
M 'A52' 'non-finite entry check removed from the matrix' $SPR 'if (!std::isfinite(values[slot])) {' 'if (values.size() > 0xFFFFFFFFFFFFULL) {'
M 'A53' 'column bound off by one' $SPR 'if (inner[slot] >= static_cast<Index>(columns)) {' 'if (inner[slot] > static_cast<Index>(columns)) {'
M 'A54' 'repeated column allowed' $SPR '!(inner[slot - 1] < inner[slot])' 'inner[slot - 1] > inner[slot]'
M 'A55' 'last row start not checked' $SPR 'rowStart.back() != static_cast<Index>(values.size()) ||' 'false ||'
M 'A56' 'symmetry bound strict' $SPR 'if (!(error <= bound)) {' 'if (!(error < bound)) {'
M 'A57' 'matrix product keeps only the last term' $SPR 'sum = sum + values_[slot] * x[static_cast<std::size_t>(inner_[slot])];' 'sum = values_[slot] * x[static_cast<std::size_t>(inner_[slot])];'
M 'A58' 'heat total keeps only the last entry' $SPR 'sum = sum + value;' 'sum = value;'
M 'A59' 'heat vector finite check removed' $SPR 'if (!std::isfinite(entries[row])) {' 'if (entries.size() > 0xFFFFFFFFFFFFULL) {'
M 'A60' 'non-square matrix reports symmetric' $SPR 'if (rows_ != columns_) {
        return std::numeric_limits<double>::infinity();' 'if (rows_ != columns_) {
        return 0.0;'
M 'A61' 'symmetry gate not applied to K' $SYS 'conductanceLayout.rowStart, conductanceLayout.inner, std::move(conductanceValues),
        kSymmetryTolerance);' 'conductanceLayout.rowStart, conductanceLayout.inner, std::move(conductanceValues));'
"" | Out-File "$s\mutation-driver-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$s\mutation-$name.log") { $prev = Get-Content "$s\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$s\mutate.ps1" -Name $name -File $e[2] -Find $e[3] -Replace $e[4] -Filter "[thermal][assembly]" | Out-Null
    $v = (Get-Content "$s\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$s\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$s\mutation-driver-progress.txt" -Append
}
"DONE" | Out-File "$s\mutation-driver-progress.txt" -Append
