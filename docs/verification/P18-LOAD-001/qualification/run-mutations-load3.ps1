# P18-LOAD-001 mutation probes, batch 3.
#   L01b..L10b  the ten formula and unit mutants of batch 1, rewritten against the
#               two lines that were reordered after it (q'' * (A/3), q''' * (V/4)),
#               so every kill is established on the final text
#   L26c, L41b  the two batch-2 survivors, rerun against the tests written for them
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalLoadVector.cpp"
$summary = "$s\mutations-load3-summary.txt"
"P18-LOAD-001 mutation probes batch 3 started $(Get-Date)" | Out-File $summary

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

$surf = "    return Power::fromSi(flux.si() * (area.si() / 3.0));"
$vol = "    return Power::fromSi(generation.si() * (volume.si() / 4.0));"
Probe "L01b-surface-share-without-the-third" $cpp $surf "    return Power::fromSi(flux.si() * area.si());"
Probe "L02b-surface-share-over-four" $cpp $surf "    return Power::fromSi(flux.si() * (area.si() / 4.0));"
Probe "L03b-volume-share-over-three" $cpp $vol "    return Power::fromSi(generation.si() * (volume.si() / 3.0));"
Probe "L04b-volume-share-without-the-quarter" $cpp $vol "    return Power::fromSi(generation.si() * volume.si());"
Probe "L06b-flux-sign-flipped" $cpp $surf "    return Power::fromSi(-flux.si() * (area.si() / 3.0));"
Probe "L07b-absolute-heat-flux" $cpp $surf "    return Power::fromSi(std::abs(flux.si()) * (area.si() / 3.0));"
Probe "L08b-absolute-generation" $cpp $vol "    return Power::fromSi(std::abs(generation.si()) * (volume.si() / 4.0));"
Probe "L09b-area-in-square-millimetres" $cpp $surf "    return Power::fromSi(flux.si() * ((area.si() * 1.0e6) / 3.0));"
Probe "L10b-volume-in-cubic-millimetres" $cpp $vol "    return Power::fromSi(generation.si() * ((volume.si() * 1.0e9) / 4.0));"
Probe "L26c-value-check-before-targets-removed" $cpp "        if (!valueIsFinite(load)) {" "        if (false && !valueIsFinite(load)) {"
Probe "L41b-running-sum-overflow-gate-removed" $cpp "    return std::isfinite(entry.si());" "    return true;"
Probe "L42-share-computed-as-product-then-quotient" $cpp $surf "    return Power::fromSi(flux.si() * area.si() / 3.0);"
"P18-LOAD-001 mutation probes batch 3 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary
