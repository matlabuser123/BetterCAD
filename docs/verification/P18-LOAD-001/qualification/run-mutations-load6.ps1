# P18-LOAD-001 mutation probes, batch 6 (one probe: L28 re-anchored on the final text).
#   L01b..L10b  the ten formula and unit mutants of batch 1, rewritten against the
#               two lines that were reordered after it (q'' * (A/3), q''' * (V/4)),
#               so every kill is established on the final text
#   L26c, L41b  the two batch-2 survivors, rerun against the tests written for them
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalLoadVector.cpp"
$summary = "$s\mutations-load6-summary.txt"
"P18-LOAD-001 mutation probes batch 6 started $(Get-Date)" | Out-File $summary

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

Probe "L28b-extra-source-term-in-the-load-vector" $cpp "    // EVERY ENTRY IS FINITE, BY CONSTRUCTION." "    if (!out.heatInput.empty()) {`n        out.heatInput[0] = Power::fromSi(out.heatInput[0].si() + 1.0);`n    }`n    // EVERY ENTRY IS FINITE, BY CONSTRUCTION."
"P18-LOAD-001 mutation probes batch 6 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary