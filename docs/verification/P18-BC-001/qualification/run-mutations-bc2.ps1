# P18-BC-001 mutation probes, batch 2. Each applies one textual mutation to the
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
$summary = "$s\mutations-bc2-summary.txt"
"P18-BC-001 mutation probes batch 2 started $(Get-Date)" | Out-File $summary
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

# --- the voids of batch 1 (the mutation left a variable or a parameter unused, which -Werror refuses), reformulated so every name is still used
Probe "B14b-robin-matrix-centroid-quadrature" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "(void)diagonal;`n            (void)off;`n            out.entries_[row * kTri3Nodes + column] = coefficient.si() * (area.si() / 9.0);"
Probe "B15b-robin-matrix-diagonally-lumped" $conv "out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;" "(void)diagonal;`n            (void)off;`n            out.entries_[row * kTri3Nodes + column] = (row == column) ? 4.0 * unit : 0.0;"
Probe "B16b-robin-source-omitted" $conv "        out.source_[row] = perNode;" "        (void)perNode;`n        out.source_[row] = 0.0;"
Probe "B26b-robin-matrix-overflow-gate-removed" $conv "    for (const double entry : out.entries_) {`n        if (!std::isfinite(entry)) {" "    for (const double entry : out.entries_) {`n        (void)entry;`n        if (false) {"
Probe "B27b-robin-source-overflow-gate-removed" $conv "    for (const double entry : out.source_) {`n        if (!std::isfinite(entry)) {" "    for (const double entry : out.source_) {`n        (void)entry;`n        if (false) {"
Probe "B28b-negative-coefficient-accepted-in-the-kernel" $conv "    if (!std::isfinite(coefficient.si()) || !(coefficient.si() >= 0.0)) {" "    (void)coefficient;`n    if (false) {"
# --- the survivor of batch 1, against the tests written for it
Probe "B25b-robin-share-formed-as-product-then-quotient" $conv "    const double perNode = (coefficient.si() * (area.si() / 3.0)) * ambient.si();" "    const double perNode = (coefficient.si() * ambient.si() * area.si()) / 3.0;"
# --- its sibling on the matrix
Probe "B53-robin-matrix-formed-as-product-then-quotient" $conv "    const double unit = coefficient.si() * (area.si() / 12.0);" "    const double unit = (coefficient.si() * area.si()) / 12.0;"
"P18-BC-001 mutation probes batch 2 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary