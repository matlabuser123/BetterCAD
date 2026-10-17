# P18-ELEM-001 mutation probes, batch 2: the five probes of batch 1 that did
# not compile under -Werror (the mutation left a function or variable unused,
# which is a void probe, not a survivor), reformulated so the mutated code
# still uses every name. Same harness, same suite, one process per case.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalElement.cpp"
$summary = "$s\mutations-elem2-summary.txt"
"P18-ELEM-001 mutation probes batch 2 started $(Get-Date)" | Out-File $summary

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

Probe "E10b-exact-zero-rule-bound-multiplied-by-zero" "    if (!(std::abs(det) > degeneracyBound(j))) {" "    if (!(std::abs(det) > degeneracyBound(j) * 0.0)) {"
Probe "E11b-absolute-degeneracy-threshold" "    if (!(std::abs(det) > degeneracyBound(j))) {" "    if (!(std::abs(det) > degeneracyBound(j) * 0.0 + 1.0e-12)) {"
Probe "E13b-gradient-finiteness-gate-removed" "            if (!std::isfinite(component)) {" "            if (!std::isfinite(component) && false) {"
Probe "E22b-capacity-lumped-on-the-diagonal" "            const double entry = row == column ? diagonal : offDiagonal;" "            const double entry = row == column ? 4.0 * offDiagonal + 0.0 * diagonal : 0.0;"
Probe "E23b-capacity-one-point-centroid-quadrature" "            const double entry = row == column ? diagonal : offDiagonal;" "            const double entry = rhoCpV / 16.0 + 0.0 * (diagonal + offDiagonal);"
# E32 SURVIVED batch 1 (the rho cp V gate was shadowed by the per-entry gate:
# same error code, same "not finite", different message). The suite now
# requires the diagnostic to name the product; the same mutant is rerun.
Probe "E32b-capacity-product-finiteness-gate-removed" "    if (!std::isfinite(rhoCpV)) {" "    if (false) {"
"P18-ELEM-001 mutation probes batch 2 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
