# P18-LOAD-001 mutation probes, batch 2.
#   L12b L14b L26b   the three probes of batch 1 that did not compile under -Werror
#                    (the mutation left a parameter or a function unused), rewritten
#                    so every name is still used
#   L18              the SURVIVOR of batch 1 (no test gave the volumetric seam a
#                    repeated element list), rerun against the new test
#   L41              the running-sum overflow gate. Batch 1's L27 showed the final
#                    scan over the vector was dead code, because this gate makes it
#                    redundant; the scan was removed, and this probe shows the gate
#                    that REPLACED it is itself tested
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalLoadVector.cpp"
$summary = "$s\mutations-load2-summary.txt"
"P18-LOAD-001 mutation probes batch 2 started $(Get-Date)" | Out-File $summary

function Probe($name, $file, $find, $replace) {
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter 'unit\.ThermalLoad_' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log" -ErrorAction SilentlyContinue
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception|\(Child aborted" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted or crashed cases $aborted" | Out-File $summary -Append
}

Probe "L12b-vector-indexed-by-node-id-value" $cpp "    return map.dofOf(node);" "    (void)map;`n    return Result<ThermalDofIndex>{ThermalDofIndex::fromValue(node.value())};"
Probe "L14b-numbering-check-removed" $cpp "    if (!map.describes(mesh)) {" "    if (false && !map.describes(mesh)) {"
Probe "L26b-non-finite-value-accepted" $cpp "        if (!valueIsFinite(load)) {" "        if (false && !valueIsFinite(load)) {"
Probe "L18-element-uniqueness-check-removed-rerun" $cpp "    if (!strictlyAscending(elements)) {" "    if (false) {"
Probe "L41-running-sum-overflow-gate-removed" $cpp "    return std::isfinite(entry.si());" "    return true;"
"P18-LOAD-001 mutation probes batch 2 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
