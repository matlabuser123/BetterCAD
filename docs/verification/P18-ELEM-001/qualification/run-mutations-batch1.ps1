# P18-ELEM-001 mutation probes. Each applies one textual mutation to the
# element kernel, rebuilds the test target, runs the element suite, records
# the verdict and restores the file. Sequential. The suite is run one process
# per test case through ctest, so a mutant that aborts the process is counted
# case by case rather than hiding its tally (the lesson of P18-DOF-001).
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalElement.cpp"
$summary = "$s\mutations-elem-summary.txt"
"P18-ELEM-001 mutation probes started $(Get-Date)" | Out-File $summary

function Probe($name, $find, $replace) {
    & "$s\mutate.ps1" -Name $name -File $cpp -Find $find -Replace $replace -Filter 'unit\.ThermalElement_' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log" -ErrorAction SilentlyContinue
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted cases $aborted" | Out-File $summary -Append
}

# --- orientation, Jacobian, gradients
Probe "E01-abs-of-detJ-accepts-inverted" "    return j[0][0] * crossX + j[1][0] * crossY + j[2][0] * crossZ;" "    return std::abs(j[0][0] * crossX + j[1][0] * crossY + j[2][0] * crossZ);"
Probe "E02-J-inverse-instead-of-J-inverse-transpose" "                sum += jInverse[k][axis] * reference[k];" "                sum += jInverse[axis][k] * reference[k];"
Probe "E03-gradient-y-z-swapped-in-storage" "            geometry.gradients_[node][axis] = sum;" "            geometry.gradients_[node][axis == 1 ? 2 : (axis == 2 ? 1 : axis)] = sum;"
Probe "E04-corners-sorted-before-the-jacobian" "    const Matrix3 j = jacobian(corners);`n    const double det = determinant(j);`n    const Matrix3 jInverse = inverse(j, det);" "    Tet4Corners sorted = corners;`n    for (std::size_t a = 1; a < kTet4Nodes; ++a) {`n        for (std::size_t b = a; b > 0 && sorted[b].x.si() < sorted[b - 1].x.si(); --b) {`n            std::swap(sorted[b], sorted[b - 1]);`n        }`n    }`n    const Matrix3 j = jacobian(sorted);`n    const double det = determinant(j);`n    const Matrix3 jInverse = inverse(j, det);"
Probe "E05-jacobian-stored-transposed" "        j[0][column] = corner.x.si() - origin.x.si();`n        j[1][column] = corner.y.si() - origin.y.si();`n        j[2][column] = corner.z.si() - origin.z.si();" "        j[column][0] = corner.x.si() - origin.x.si();`n        j[column][1] = corner.y.si() - origin.y.si();`n        j[column][2] = corner.z.si() - origin.z.si();"
Probe "E06-millimetre-conversion-in-the-jacobian" "        j[0][column] = corner.x.si() - origin.x.si();" "        j[0][column] = (corner.x.si() - origin.x.si()) * 1.0e3;"
Probe "E07-volume-is-detJ-not-detJ-over-six" "    geometry.volume_ = Volume::fromSi(det / 6.0);" "    geometry.volume_ = Volume::fromSi(det);"
Probe "E08-inverted-tet-accepted" "    if (det < 0.0) {`n        return ElementProblem::InvertedElement;`n    }`n    return std::nullopt;" "    if (false) {`n        return ElementProblem::InvertedElement;`n    }`n    return std::nullopt;"
Probe "E09-even-permutation-rejected" "    if (det < 0.0) {`n        return ElementProblem::InvertedElement;`n    }`n    return std::nullopt;" "    if (det < 0.0 || corners[0].x.si() > corners[1].x.si()) {`n        return ElementProblem::InvertedElement;`n    }`n    return std::nullopt;"
# --- degeneracy
Probe "E10-exact-zero-rule-instead-of-the-noise-bound" "    if (!(std::abs(det) > degeneracyBound(j))) {" "    if (det == 0.0) {"
Probe "E11-absolute-degeneracy-threshold" "    if (!(std::abs(det) > degeneracyBound(j))) {" "    if (!(std::abs(det) > 1.0e-12)) {"
Probe "E12-volume-underflow-test-removed" "    if (det / 6.0 == 0.0) {" "    if (false) {"
Probe "E13-gradient-finiteness-gate-removed" "            if (!std::isfinite(component)) {" "            if (false) {"
# --- conduction
Probe "E14-volume-omitted-from-Ke" "    const double kv = conductivity.si() * geometry.volume().si();" "    const double kv = conductivity.si();"
Probe "E15-Ke-uses-six-volumes" "    const double kv = conductivity.si() * geometry.volume().si();" "    const double kv = conductivity.si() * geometry.determinant().si();"
Probe "E16-epsilon-added-to-the-Ke-diagonal" "            const double entry = kv * dot;" "            const double entry = kv * dot + (row == column ? 1.0e-6 * kv : 0.0);"
Probe "E17-Ke-averaged-with-an-incomplete-dot" "            const double entry = kv * dot;" "            const double entry = 0.5 * kv * (dot + gi[0].si() * gj[0].si() + gi[1].si() * gj[1].si());"
Probe "E18-one-diagonal-scaled-null-mode-broken" "            const double entry = kv * dot;" "            const double entry = kv * dot * (row == 0 && column == 0 ? 1.01 : 1.0);"
Probe "E19-negative-conductivity-accepted" "    return std::isfinite(value) && value > 0.0;" "    return std::isfinite(value);"
Probe "E20-infinite-conductivity-accepted" "    return std::isfinite(value) && value > 0.0;" "    return value > 0.0;"
Probe "E21-Ke-finiteness-gate-removed" "            if (!std::isfinite(entry)) {`n                return makeError(ErrorCode::FailedPrecondition,`n                                 std::format(`"conduction entry" "            if (false) {`n                return makeError(ErrorCode::FailedPrecondition,`n                                 std::format(`"conduction entry"
# --- capacity
Probe "E22-capacity-lumped-on-the-diagonal" "            const double entry = row == column ? diagonal : offDiagonal;" "            const double entry = row == column ? 4.0 * offDiagonal : 0.0;"
Probe "E23-capacity-one-point-centroid-quadrature" "            const double entry = row == column ? diagonal : offDiagonal;" "            const double entry = rhoCpV / 16.0;"
Probe "E24-capacity-off-diagonal-equals-diagonal" "            const double entry = row == column ? diagonal : offDiagonal;" "            const double entry = diagonal;"
Probe "E25-capacity-diagonal-half-of-off-diagonal" "    const double diagonal = 2.0 * offDiagonal;" "    const double diagonal = 0.5 * offDiagonal;"
Probe "E26-capacity-over-ten-not-twenty" "    const double offDiagonal = rhoCpV / 20.0;" "    const double offDiagonal = rhoCpV / 10.0;"
Probe "E27-density-omitted" "    const double rhoCpV = capacity.density.si() * capacity.specificHeatCapacity.si() *`n                          geometry.volume().si();" "    const double rhoCpV = capacity.specificHeatCapacity.si() * geometry.volume().si();"
Probe "E28-specific-heat-omitted" "    const double rhoCpV = capacity.density.si() * capacity.specificHeatCapacity.si() *`n                          geometry.volume().si();" "    const double rhoCpV = capacity.density.si() * geometry.volume().si();"
Probe "E29-capacity-sums-rho-and-cp" "    const double rhoCpV = capacity.density.si() * capacity.specificHeatCapacity.si() *`n                          geometry.volume().si();" "    const double rhoCpV = (capacity.density.si() + capacity.specificHeatCapacity.si()) *`n                          geometry.volume().si();"
Probe "E30-absolute-density-accepted" "    if (!isUsable(capacity.density.si())) {" "    if (!isUsable(std::abs(capacity.density.si()))) {"
Probe "E31-absolute-specific-heat-accepted" "    if (!isUsable(capacity.specificHeatCapacity.si())) {" "    if (!isUsable(std::abs(capacity.specificHeatCapacity.si()))) {"
Probe "E32-capacity-product-finiteness-gate-removed" "    if (!std::isfinite(rhoCpV)) {" "    if (false) {"
Probe "E33-capacity-uses-the-conduction-volume-factor" "    const double offDiagonal = rhoCpV / 20.0;" "    const double offDiagonal = rhoCpV / 20.0 * 6.0;"
"P18-ELEM-001 mutation probes finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
