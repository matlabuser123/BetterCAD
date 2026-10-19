# P18-BC-001 mutation probes, batch 3. Each applies one textual mutation to the
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
$summary = "$s\mutations-bc3-summary.txt"
"P18-BC-001 mutation probes batch 3 started $(Get-Date)" | Out-File $summary
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

Probe "B54-robin-matrix-omitted-source-only" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "(void)diagonal;`n            (void)off;`n            out.entries_[row * kTri3Nodes + column] = 0.0;"
Probe "B55-one-convection-facet-dropped" $bnd "    out.reserve(facets.size());`n    for (const meshing::ElementId id : facets) {" "    out.reserve(facets.size());`n    for (const meshing::ElementId id : (facets.size() > 1 ? facets.subspan(1) : facets)) {"
Probe "B56-one-convection-facet-counted-twice" $bnd "        out.push_back(ConvectionFacet{.facet = id, .dofs = dofs, .term = std::move(*term)});" "        out.push_back(ConvectionFacet{.facet = id, .dofs = dofs, .term = std::move(*term)});`n        if (out.size() == 1) {`n            ConvectionFacet again = out.back();`n            out.push_back(std::move(again));`n        }"
Probe "B57-node-id-value-used-as-the-unknown" $bnd "            dofs[corner] = *dof;" "            dofs[corner] = ThermalDofIndex::fromValue(triangle->nodes[corner].value());"
Probe "B58-unresolved-condition-silently-skipped" $bnd "            case TargetProblem::Unresolved:`n                return std::unexpected(failure(" "            case TargetProblem::Unresolved:`n                continue;`n                return std::unexpected(failure("
"P18-BC-001 mutation probes batch 3 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary