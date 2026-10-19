# P18-BC-001 mutation probes, batch 4 (representative sample re-run after the export fix). Each applies one textual mutation to the
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
$summary = "$s\mutations-bc4-summary.txt"
"P18-BC-001 mutation probes batch 4 (representative sample re-run after the export fix) started $(Get-Date)" | Out-File $summary
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

Probe "B14c-robin-matrix-centroid-quadrature" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "(void)diagonal;`n            (void)off;`n            out.entries_[row * kTri3Nodes + column] = coefficient.si() * (area.si() / 9.0);"
Probe "B18b-robin-matrix-negated" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "out.entries_[row * kTri3Nodes + column] = -((row == column) ? diagonal : off);"
Probe "B04b-first-conflicting-temperature-wins" $bnd "    Result<ThermalConstraintMap> built = buildThermalConstraintMap(map, wanted);" "    { std::vector<ThermalTemperatureConstraint> kept; for (const auto& w : wanted) { bool seen = false; for (const auto& k : kept) { if (k.dof.value() == w.dof.value()) { seen = true; } } if (!seen) { kept.push_back(w); } } wanted = std::move(kept); }`n    Result<ThermalConstraintMap> built = buildThermalConstraintMap(map, wanted);"
Probe "B35b-identical-convections-deduplicated" $bnd "            const ConvectionBoundary* convection = condition.convection();" "            const ConvectionBoundary* convection = condition.convection();`n            if (std::ranges::any_of(convections, [&](const PreparedConvectionBoundary& p) { return p.coefficient.si() == convection->coefficient.si() && p.ambient.si() == convection->ambient.si(); })) { continue; }"
Probe "B43b-mesh-stamp-omitted-from-the-source" $bnd "    source.mesh = model.mesh().mesh().stamp();" "    source.mesh = meshing::MeshStamp{};"
"P18-BC-001 mutation probes batch 4 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary