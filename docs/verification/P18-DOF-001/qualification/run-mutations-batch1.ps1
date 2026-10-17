# P18-DOF-001 mutation probes. Each applies one textual mutation to a
# production source, rebuilds, runs the thermal DOF suite, records the verdict
# and restores the file. Sequential.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalDof.cpp"
$hpp = "$root\include\bettercad\thermal\ThermalDof.hpp"
$filter = "[thermal][dof]"
$summary = "$s\mutations-dof-summary.txt"
"P18-DOF-001 mutation probes started $(Get-Date)" | Out-File $summary

function Probe($name, $file, $find, $replace, $filterOverride) {
    $f = if ($filterOverride) { $filterOverride } else { $filter }
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter $f | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = ($log | Select-String -Pattern "^TESTS").Line
    "$name : $verdict : $tests" | Out-File $summary -Append
}

# Raw NodeId arithmetic and allocation
Probe "D01-dof-is-the-raw-node-id" $cpp "            return ThermalDofIndex::fromValue(`n                static_cast<ThermalDofIndex::ValueType>(found - nodes_.begin()));" "            return ThermalDofIndex::fromValue(static_cast<ThermalDofIndex::ValueType>(node.value()));"
Probe "D02-dof-count-is-max-node-id-plus-one" $hpp "        return static_cast<ThermalDofIndex::ValueType>(kThermalDofsPerNode) *`n               static_cast<ThermalDofIndex::ValueType>(nodes_.size());" "        return nodes_.empty() ? 0 : static_cast<ThermalDofIndex::ValueType>(nodes_.back().value()) + 1;"
Probe "D03-nodes-reversed-instead-of-mesh-order" $cpp "        map.nodes_.push_back(node.id);`n    }`n    return map;" "        map.nodes_.push_back(node.id);`n    }`n    std::ranges::reverse(map.nodes_);`n    return map;"
# Source binding
Probe "D04-describes-ignores-the-stamp" $hpp "        return mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size();" "        return mesh.nodes().size() == nodes_.size();"
Probe "D05-describes-ignores-the-count" $hpp "        return mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size();" "        return mesh.owns(stamp_);"
Probe "D06-free-map-accepts-same-counts-as-same-mesh" $cpp "    if (map.stamp() != constraints.stamp() || map.dofCount() != constraints.dofCount()) {" "    if (map.dofCount() != constraints.dofCount()) {"
Probe "D07-free-map-ignores-the-count" $cpp "    if (map.stamp() != constraints.stamp() || map.dofCount() != constraints.dofCount()) {" "    if (map.stamp() != constraints.stamp()) {"
Probe "D08-agreesWith-ignores-the-complement" $cpp "    if (freeDofs_.size() + constraints.size() != static_cast<std::size_t>(dofCount_)) {`n        return false;`n    }`n    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {`n        if (std::ranges::binary_search(freeDofs_, constraint.dof)) {`n            return false;`n        }`n    }`n    return true;" "    return true;"
# Constraint values
Probe "D09-constraint-map-loses-the-temperature" $cpp "    return found->temperature;`n}" "    return Temperature{};`n}"
Probe "D10-constrained-temperature-forced-to-zero" $cpp "            full.push_back(constrained[nextConstrained].temperature);" "            full.push_back(Temperature{});"
Probe "D11-duplicate-constraint-counted-twice" $cpp "            if (unique.back().temperature == constraint.temperature) {`n                continue;`n            }" "            if (unique.back().temperature == constraint.temperature) {`n                unique.push_back(constraint);`n                continue;`n            }"
Probe "D12-last-conflicting-value-wins" $cpp "            out.problem = ConstraintProblem::ConflictingTemperature;" "            unique.back() = constraint;`n            continue;`n            out.problem = ConstraintProblem::ConflictingTemperature;"
Probe "D13-nan-temperature-accepted" $cpp "        if (!isAbsoluteTemperature(constraint.temperature)) {" "        if (false && !isAbsoluteTemperature(constraint.temperature)) {"
# Free equations
Probe "D14-free-equation-equals-dof" $cpp "    return ThermalFreeEquationIndex::fromValue(`n        static_cast<ThermalFreeEquationIndex::ValueType>(found - freeDofs_.begin()));" "    return ThermalFreeEquationIndex::fromValue(dof.value());"
Probe "D15-constrained-dof-gets-equation-zero" $cpp "    const auto found = std::ranges::lower_bound(freeDofs_, dof);`n    if (found == freeDofs_.end() || *found != dof) {`n        return std::nullopt;`n    }" "    const auto found = std::ranges::lower_bound(freeDofs_, dof);`n    if (found == freeDofs_.end() || *found != dof) {`n        return ThermalFreeEquationIndex::fromValue(0);`n    }"
# Element-local order
Probe "D16-tet-local-dofs-sorted" $cpp "    return std::array<ThermalDofIndex, 4>{*d0, *d1, *d2, *d3};" "    std::array<ThermalDofIndex, 4> sorted{*d0, *d1, *d2, *d3};`n    std::ranges::sort(sorted);`n    return sorted;"
Probe "D17-per-element-dofs" $cpp "    return std::array<ThermalDofIndex, 4>{*d0, *d1, *d2, *d3};" "    return std::array<ThermalDofIndex, 4>{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(1), ThermalDofIndex::fromValue(2), ThermalDofIndex::fromValue(3)};"
"P18-DOF-001 mutation probes finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
