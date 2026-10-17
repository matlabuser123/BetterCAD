# P18-DOF-001 mutation probes, batch 2: the four probes the first batch
# killed by process abort (an unchecked optional dereference in the TEST
# under the mutant), rerun against the hardened test file so the kill is
# an assertion failure where the test can reach one.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalDof.cpp"
$hpp = "$root\include\bettercad\thermal\ThermalDof.hpp"
$filter = "[thermal][dof]"
$summary = "$s\mutations-dof2-summary.txt"
"P18-DOF-001 mutation probes batch 2 started $(Get-Date)" | Out-File $summary

function Probe($name, $file, $find, $replace, $filterOverride) {
    $f = if ($filterOverride) { $filterOverride } else { $filter }
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter $f | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = ($log | Select-String -Pattern "^TESTS").Line
    "$name : $verdict : $tests" | Out-File $summary -Append
}

Probe "D01b-dof-is-the-raw-node-id" $cpp "            return ThermalDofIndex::fromValue(`n                static_cast<ThermalDofIndex::ValueType>(found - nodes_.begin()));" "            return ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(node.value()));"
Probe "D02b-dof-count-is-max-node-id-plus-one" $hpp "        return static_cast<ThermalDofIndex::ValueType>(kThermalDofsPerNode) *`n               static_cast<ThermalDofIndex::ValueType>(nodes_.size());" "        return nodes_.empty() ? 0 : static_cast<ThermalDofIndex::ValueType>(nodes_.back().value()) + 1;"
Probe "D03b-nodes-reversed-instead-of-mesh-order" $cpp "        map.nodes_.push_back(node.id);`n    }`n    return map;" "        map.nodes_.push_back(node.id);`n    }`n    std::ranges::reverse(map.nodes_);`n    return map;"
Probe "D08b-agreesWith-ignores-the-complement" $cpp "    if (freeDofs_.size() + constraints.size() != static_cast<std::size_t>(dofCount_)) {`n        return false;`n    }`n    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {`n        if (std::ranges::binary_search(freeDofs_, constraint.dof)) {`n            return false;`n        }`n    }`n    return true;" "    return true;"
"P18-DOF-001 mutation probes batch 2 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
