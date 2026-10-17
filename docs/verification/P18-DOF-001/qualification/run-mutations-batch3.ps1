# P18-DOF-001 mutation probes, batch 3: D02 and D08 once more, this time one
# process per test case through ctest, because under both mutants the
# PRODUCTION code indexes past its storage (libstdc++ assertion, abort) and
# the abort discards Catch2's buffered stdout, so a single-process run cannot
# say how many cases failed by assertion before the abort. ctest records each
# case's own exit: Passed, Failed, or Subprocess aborted.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalDof.cpp"
$hpp = "$root\include\bettercad\thermal\ThermalDof.hpp"
$summary = "$s\mutations-dof3-summary.txt"
"P18-DOF-001 mutation probes batch 3 (per-case via ctest) started $(Get-Date)" | Out-File $summary

function ProbeCtest($name, $file, $find, $replace) {
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter 'unit\.ThermalDof' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log"
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted cases $aborted" | Out-File $summary -Append
}

ProbeCtest "D02c-dof-count-is-max-node-id-plus-one" $hpp "        return static_cast<ThermalDofIndex::ValueType>(kThermalDofsPerNode) *`n               static_cast<ThermalDofIndex::ValueType>(nodes_.size());" "        return nodes_.empty() ? 0 : static_cast<ThermalDofIndex::ValueType>(nodes_.back().value()) + 1;"
ProbeCtest "D08c-agreesWith-ignores-the-complement" $cpp "    if (freeDofs_.size() + constraints.size() != static_cast<std::size_t>(dofCount_)) {`n        return false;`n    }`n    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {`n        if (std::ranges::binary_search(freeDofs_, constraint.dof)) {`n            return false;`n        }`n    }`n    return true;" "    return true;"
"P18-DOF-001 mutation probes batch 3 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
