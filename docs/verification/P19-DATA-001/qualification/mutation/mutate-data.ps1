# P19-DATA-001 mutation campaign. Each probe replaces ONE exact text in ONE file, rebuilds bettercad_tests (debug-ext), runs the CFD cases
# through CTest (^unit\.(CfdData_|CfdInput_)), records the verdict and ALWAYS restores the file byte for byte. KILLED = some case failed;
# SURVIVED = all passed; VOID = the mutant did not compile; NOT FOUND = the target text is absent (a script defect, not a verdict).
# ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([string]$Only = "")
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\mutation-data"
New-Item -ItemType Directory -Force $s | Out-Null
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$pk = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$env:Path = "$pk\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$tree = "C:\Users\hasib\bcdev\bc-build\debug-ext"
$data = "$root\src\cfd\CfdData.cpp"
$object = "$root\src\cfd\CfdAnalysisObject.cpp"
$result = "$root\src\cfd\CfdResult.cpp"
$probes = @(
    @("DP01", $data, "a zero inflow speed is accepted", "inlet->normalSpeed.si() <= 0.0", "inlet->normalSpeed.si() < 0.0"),
    @("DP02", $data, "a NaN inflow speed is accepted", "if (!isFinite(inlet->normalSpeed) || inlet->normalSpeed.si() <= 0.0)", "if (inlet->normalSpeed.si() <= 0.0)"),
    @("DP03", $data, "a moving wall at rest is accepted", "if (wall->velocity.magnitude().si() == 0.0)", "if (false)"),
    @("DP04", $data, "a velocity relaxation above 1 is accepted", "!isPositiveRatio(settings.velocityRelaxation) || settings.velocityRelaxation > 1.0", "!isPositiveRatio(settings.velocityRelaxation)"),
    @("DP05", $data, "a divergence factor of 1 is accepted", "settings.divergenceFactor <= 1.0", "settings.divergenceFactor <= 0.0"),
    @("DP06", $data, "an unknown face role is accepted", "if (toString(condition.face().face.role) == ""unknown"")", "if (false)"),
    @("DP07", $data, "a pressure inlet does not fix the pressure", "return pressureInlet() != nullptr || pressureOutlet() != nullptr;", "return pressureOutlet() != nullptr;"),
    @("DP08", $data, "the gravity reference height is not checked", "if (!isFinite(gravity.referenceHeight))", "if (false)"),
    @("DP09", $object, "an overdetermined datum is accepted", "if (pressureDatumProblem(definition) == PressureDatumProblem::Overdetermined)", "if (false)"),
    @("DP10", $object, "a steady definition may carry transient settings", "        if (definition.transient.has_value()) {`n            return makeError(ErrorCode::InvalidArgument,`n                             ""CFD analysis: a steady analysis has no time step or end time"");`n        }`n", ""),
    @("DP11", $object, "the reference location is not written", "point(definition.referencePressure->location)", "point(Point3D{})"),
    @("DP12", $object, "the gravity reference height is not written", "        line(out, ""gravity.reference_height"", number(definition.gravity->referenceHeight));`n", ""),
    @("DP13", $object, "-0.0 is not written as 0", "return std::format(""{}"", value == 0.0 ? 0.0 : value);", "return std::format(""{}"", value);"),
    @("DP14", $object, "a copy's instance is not written", "copies += std::format(""{}:{}"", copy.feature.value(), copy.instance);", "copies += std::format(""{}:"", copy.feature.value());"),
    @("DP15", $object, "a fluid revision change is not a stale reason", "result.fluid != current.fluid || result.fluidRevision != current.fluidRevision", "result.fluid != current.fluid"),
    @("DP16", $object, "an equal definition moves the revision", "        // NO EFFECTIVE CHANGE: no revision moves and no result is invalidated.`n        return false;", "        return true;"),
    @("DP17", $result, "a missing pressure datum is Ready", "if (pressureDatumProblem(intent->definition()) == PressureDatumProblem::Missing)", "if (false)"),
    @("DP18", $result, "a failure discards the held result", "    failure_ = std::move(failure);`n}", "    failure_ = std::move(failure);`n    result_.reset();`n}"),
    @("DP19", $result, "a failure does not outrank a held result", "    if (lastFailure != nullptr) {", "    if (lastFailure != nullptr && lastResult == nullptr) {"),
    @("DP20", $result, "the source carries no fingerprint", ".definition = fingerprint(intent->definition()),", ".definition = CfdDefinitionFingerprint{},")
)
$progress = "$s\progress.txt"
foreach ($p in $probes) {
    $name, $file, $what, $find, $replace = $p
    if ($Only -and $name -ne $Only) { continue }
    $log = "$s\$name.log"
    $original = [IO.File]::ReadAllText($file)
    # The sources use LF; the probe texts are written with `n.
    if (-not $original.Contains($find)) {
        "$name | $what | NOT FOUND" | Add-Content $progress
        continue
    }
    [IO.File]::WriteAllText($file, $original.Replace($find, $replace), (New-Object Text.UTF8Encoding $false))
    $verdict = "UNKNOWN"; $summary = ""; $failed = ""
    try {
        $buildOut = & cmake --build $tree --target bettercad_tests 2>&1
        $buildOut | Out-File "$s\$name-build.log" -Width 400
        if ($LASTEXITCODE -ne 0) {
            $verdict = "VOID (did not compile)"
        } else {
            $ctestOut = & ctest --test-dir $tree -R "^unit\.(CfdData_|CfdInput_)" -j 6 --timeout 600 2>&1
            $ctestOut | Out-File "$s\$name-tests.log" -Width 400
            $summary = ($ctestOut | Select-String -Pattern "tests passed").Line -join " "
            $failed = ($ctestOut | Select-String -Pattern "^\s*\d+ - unit\.(\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join "; "
            $verdict = if (($ctestOut | Out-String) -match "100% tests passed") { "SURVIVED" } else { "KILLED" }
        }
    } finally {
        [IO.File]::WriteAllText($file, $original, (New-Object Text.UTF8Encoding $false))
    }
    $restored = if ([IO.File]::ReadAllText($file) -ceq $original) { "restored" } else { "NOT RESTORED" }
    "$name | $what | $verdict | $summary | $failed | $restored" | Add-Content $progress
}
"campaign finished $(Get-Date -Format o)" | Add-Content $progress
