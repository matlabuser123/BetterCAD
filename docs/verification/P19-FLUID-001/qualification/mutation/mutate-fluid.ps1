# P19-FLUID-001 mutation campaign. Each probe replaces ONE exact text in ONE file, rebuilds bettercad_tests (debug-ext), runs the CFD cases
# through CTest (the CFD, fluid and P15 material suites and the cli.material process tests), records the verdict and ALWAYS restores the file byte for byte. KILLED = some case failed;
# SURVIVED = all passed; VOID = the mutant did not compile; NOT FOUND = the target text is absent (a script defect, not a verdict).
# ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([string]$Only = "")
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\mutation-fluid"
New-Item -ItemType Directory -Force $s | Out-Null
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$pk = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$env:Path = "$pk\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$tree = "C:\Users\hasib\bcdev\bc-build\debug-ext"
$thermal = "$root\src\core\materials\ThermalProperties.cpp"
$complete = "$root\src\core\materials\Completeness.cpp"
$features = "$root\src\features\material\Materials.cpp"
$json = "$root\src\io\json\MaterialJson.cpp"
$vocab = "$root\apps\bettercad_cli\MaterialVocabulary.cpp"
$fluid = "$root\src\cfd\CfdFluid.cpp"
$boundary = "$root\src\cfd\CfdAnalysis.cpp"
$result = "$root\src\cfd\CfdResult.cpp"
$probes = @(
    @("FP01", $complete, "the flow row does not require a viscosity", ".thermal = {ThermKind::DynamicViscosity}};", ".thermal = {}};"),
    @("FP02", $complete, "a viscosity is always available", "            return positiveQuantity(properties.dynamicViscosity);", "            return true;"),
    @("FP03", $thermal, "a non-positive viscosity is accepted at entry", "    checkPositive(properties.dynamicViscosity, ""dynamic viscosity"", problems);", ""),
    @("FP04", $thermal, "the viscosity's reference temperature is not checked", "    checkReferenceTemperature(properties.dynamicViscosity, ""dynamic viscosity"", problems);", ""),
    @("FP05", $thermal, "a Derived viscosity is accepted", "    checkNotDerived(properties.dynamicViscosity, ""dynamic viscosity"", problems);", ""),
    @("FP06", $thermal, "hasDynamicViscosity trusts any value", "    return mu && isFinite(*mu) && mu->si() > 0.0;", "    return mu.has_value();"),
    @("FP07", $features, "the flow function hands over a default viscosity", "    properties.dynamicViscosity = *mu;", "    properties.dynamicViscosity = DynamicViscosity::fromSi(1.0e-3);"),
    @("FP08", $features, "a viscosity edit keeps its stale citation", "                return before.dynamicViscosity != after.dynamicViscosity;", "                return false;"),
    @("FP09", $features, "every thermal edit drops the viscosity citation", "                return before.dynamicViscosity != after.dynamicViscosity;", "                return true;"),
    @("FP10", $json, "the file writes the viscosity in mPa s", "propertyToJson(properties.dynamicViscosity, value->si())", "propertyToJson(properties.dynamicViscosity, value->si() * 1000.0)"),
    @("FP11", $json, "the file reader drops the viscosity", "    if (*viscosity) {", "    if (false) {"),
    @("FP12", $vocab, "a bare CLI number is mPa s", "auto value = parseQuantity(text, units::Pa_s);", "auto value = parseQuantity(text, units::mPa_s);"),
    @("FP13", $vocab, "the CLI shows the viscosity in mPa s", "return inUnit(value, units::Pa_s); });", "return inUnit(value, units::mPa_s); });"),
    @("FP14", $fluid, "a density gap is reported as a viscosity problem", "                return fail(FluidProblem::DensityUnavailable, flow.error());", "                return fail(FluidProblem::ViscosityUnavailable, flow.error());"),
    @("FP15", $fluid, "the fluid carries no revision", "                    .revision = (*material)->revision(),", "                    .revision = 0,"),
    @("FP16", $boundary, "a viscosity gap is reported as a density problem", "        return fail(InputProblem::FluidViscosityUnusable, fluid.error());", "        return fail(InputProblem::FluidDensityUnusable, fluid.error());"),
    @("FP17", $result, "the source records no fluid revision", "        .fluidRevision = model->fluid().revision(),", "        .fluidRevision = 1,"),
    @("FP18", $features, "requireDynamicViscosity accepts a non-positive value", "    if (!isFinite(*mu) || mu->si() <= 0.0) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format(""{} has a dynamic viscosity of {}, which is not a usable value"",", "    if (false) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format(""{} has a dynamic viscosity of {}, which is not a usable value"",")
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
        $buildOut = & cmake --build $tree --target bettercad_tests bettercad_cli 2>&1
        $buildOut | Out-File "$s\$name-build.log" -Width 400
        if ($LASTEXITCODE -ne 0) {
            $verdict = "VOID (did not compile)"
        } else {
            $ctestOut = & ctest --test-dir $tree -R "^(unit\.(CfdFluid_|FluidProperty_|CfdInput_|CfdData_|Provenance_|ThermalProperty_|MaterialFile|CustomMaterial_|MaterialCli_)|cli\.material\.)" -j 6 --timeout 600 2>&1
            $ctestOut | Out-File "$s\$name-tests.log" -Width 400
            $summary = ($ctestOut | Select-String -Pattern "tests passed").Line -join " "
            $failed = ($ctestOut | Select-String -Pattern "^\s*\d+ - (\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join "; "
            $verdict = if (($ctestOut | Out-String) -match "100% tests passed") { "SURVIVED" } else { "KILLED" }
        }
    } finally {
        [IO.File]::WriteAllText($file, $original, (New-Object Text.UTF8Encoding $false))
    }
    $restored = if ([IO.File]::ReadAllText($file) -ceq $original) { "restored" } else { "NOT RESTORED" }
    "$name | $what | $verdict | $summary | $failed | $restored" | Add-Content $progress
}
"campaign finished $(Get-Date -Format o)" | Add-Content $progress
