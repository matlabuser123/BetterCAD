# After P18-CMD-001's three-preset qualification: measure each preset's own binaries and run the thermal command suite (and its neighbours) on
# each, then compare the CMDFP fingerprint lines across the presets. Writes nothing into the source tree.
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-cmd"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-CMD-001\qualification"
$summary = Join-Path $out "summary.txt"
"postrun-cmd started $(Get-Date -Format o)" | Out-File $summary
"== layering checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
"== thermal command ownership checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckThermalCommandOwnership.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
"== warnings and errors in the harness build logs" | Out-File $summary -Append
foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
    $log = Join-Path $q "build-$preset.log"
    $w = (Select-String -Path $log -Pattern "warning:" | Measure-Object).Count
    $e = (Select-String -Path $log -Pattern "error:" | Measure-Object).Count
    $objects = (Select-String -Path $log -Pattern "Building CXX object" | Measure-Object).Count
    $steps = (Select-String -Path $log -Pattern "^\[\d+/(\d+)\]" | Select-Object -Last 1).Line
    "  $preset : warning: $w  error: $e  objects $objects  last step $steps" | Out-File $summary -Append
    $rebuild = Get-Content (Join-Path $q "rebuild-$preset.log") | Select-String -Pattern "^\[" | Measure-Object | Select-Object -ExpandProperty Count
    "  $preset no-op rebuild edges: $rebuild" | Out-File $summary -Append
    "  $preset clean: " + ((Get-Content (Join-Path $q "clean-$preset.log") | Select-String -Pattern "files").Line) | Out-File $summary -Append
    "  $preset ctest: " + ((Get-Content (Join-Path $q "ctest-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | ") | Out-File $summary -Append
}
foreach ($preset in "release-ext", "debug-ext") {
    "  repeat $preset : " + ((Get-Content (Join-Path $q "ctest-repeat-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | ") | Out-File $summary -Append
}
"== harness stderr lines: " + (Get-Content (Join-Path $q "run-qualification.err") | Measure-Object -Line).Lines | Out-File $summary -Append
"== non-CRLF stderr lines: " + (Get-Content (Join-Path $q "run-qualification.err") | Where-Object { $_ -notmatch "CRLF" -and $_.Trim() -ne "" } | Measure-Object -Line).Lines | Out-File $summary -Append
$filters = @(
    'unit\.ThermalCommand_',
    'unit\.ThermalCommand_(CreateAnalysis|CreateAnalysisUndoExact|CreateAnalysisRedoExact|CreateAnalysisRedoPreservesId|CreateAnalysisCreatesIntentOnly|CreateAnalysisFailsAtomically|CreateThenDependentCommandsUndoRedoEverything|CreateKeepsTheMeshHandleAsGiven)$',
    'unit\.ThermalCommand_(AddLoadUndoRedo|AddVolumetricLoadUndoRedo|EditLoadUndoRedo|EditLoadMayChangeItsKind|RemoveLoadUndoRedo|MiddleRemovalRestoresExactCanonicalOrder|AnEditKeepsThePositionOfTheLoad|LoadIdPreserved|LoadGeometryReferencePreserved|LoadSignedMagnitudePreserved|EditMissingStableIdFails|AnEditOfARemovedLoadDoesNotAliasTheNextOne|FailedLoadCommandsAreAtomic|ALoadMayBeDefinedBeforeAnyMeshExists|ATargetThatNamesNoFeatureIsRefused)$',
    'unit\.ThermalCommand_(Undo|Redo|MultiLevel|Branching|Repeated|NoValue|Heterogeneous|ASigned|AnUndo)',
    'unit\.ThermalCommand_.*(Currentness|Invalidat|Stale|Revive|Regime|Matrix|Remesh|Result)',
    'unit\.ThermalCommand_(HistorySurvivesARemesh|HistorySizeIndependentOfThermalResultSize)$',
    'compile_fail\.thermalcmd\.', 'architecture\.thermalcmd-ownership',
    'unit\.ThermalViz', 'compile_fail\.thermalviz\.', 'architecture\.thermalviz-ownership',
    'unit\.ThermalValidation', 'architecture\.validation-ownership',
    'unit\.StructuralCommand', 'unit\.Structural', 'unit\.View', 'unit\.Mesh',
    'unit\.ThermalBalance', 'unit\.ThermalPost', 'unit\.ThermalTransient', 'unit\.ThermalSolve', 'unit\.Linalg_', 'architecture\.targets\.',
    'unit\.Thermal', '^architecture\.'
)
foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
    $bin = "$root\$preset\bin"
    "== $preset" | Out-File $summary -Append
    foreach ($exe in "bettercad-cli.exe", "bettercad_example_reference_models.exe", "bettercad_tests.exe") {
        $f = Get-Item (Join-Path $bin $exe)
        $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        "  $exe $($f.Length) $h" | Out-File $summary -Append
    }
    foreach ($dll in "libbettercad_thermal.dll", "libbettercad_core.dll") {
        if (Test-Path (Join-Path $bin $dll)) {
            $f = Get-Item (Join-Path $bin $dll)
            "  $dll $($f.Length) " + (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower() | Out-File $summary -Append
        }
    }
    if (Test-Path (Join-Path $bin "libbettercad_thermal.dll")) {
        "  dll count: " + (Get-ChildItem $bin -Filter "libbettercad_*.dll" | Measure-Object).Count | Out-File $summary -Append
    }
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    "  ctest -N: $n" | Out-File $summary -Append
    foreach ($filter in $filters) {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    # THE COMMAND SUITE through the XML reporter: the console reporter wraps a long line in the middle of a token.
    $xmlFile = Join-Path $out "cmd-$preset.xml"
    & (Join-Path $bin "bettercad_tests.exe") "ThermalCommand_*" -r xml > $xmlFile 2>&1
    $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
    "  ThermalCommand_* : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value | Out-File $summary -Append
    $lines = [regex]::Matches($xml, '(CMDFP) [^\r\n<]*') | ForEach-Object { $_.Value }
    $lines | Out-File (Join-Path $out "cmd-messages-$preset.txt") -Encoding utf8
    "  evidence lines: $($lines.Count)" | Out-File $summary -Append
    foreach ($suite in @(@("ThermalViz_*", "viz"), @("ThermalValidation_*", "validation"), @("ThermalBalance_*", "balance"), @("ThermalPost_*", "post"), @("ThermalTransient_*", "transient"), @("StructuralCommand_*", "structcmd"), @("[structural]", "structural"), @("[meshing]", "meshing"))) {
        $file = Join-Path $out "$($suite[1])-$preset.txt"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] > $file 2>&1
        "  $($suite[0]) exit $LASTEXITCODE : " + ((Get-Content $file | Select-String "^test cases:|^assertions:|^All tests passed").Line -join " | ") | Out-File $summary -Append
    }
}
"== cross-preset classification (every CMDFP line of the command suite)" | Out-File $summary -Append
$ref = Get-Content (Join-Path $out "cmd-messages-debug-ext.txt") | Where-Object { $_ -match "^CMDFP " } | Sort-Object
"  debug-ext CMDFP lines: $($ref.Count)" | Out-File $summary -Append
foreach ($l in $ref) { "    $l" | Out-File $summary -Append }
foreach ($preset in "release-ext", "debug-shared-ext") {
    $other = Get-Content (Join-Path $out "cmd-messages-$preset.txt") | Where-Object { $_ -match "^CMDFP " } | Sort-Object
    $diff = Compare-Object $ref $other
    "  CMDFP lines, debug-ext vs $preset : $($ref.Count) vs $($other.Count), differing: $(($diff | Measure-Object).Count)" | Out-File $summary -Append
    $diff | Select-Object -First 8 | ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" } | Out-File $summary -Append
}
"postrun-cmd finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
