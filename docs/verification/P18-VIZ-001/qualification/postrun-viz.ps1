# After P18-VIZ-001's three-preset qualification: measure each preset's own binaries and run the thermal visualization suite (and its neighbours)
# on each, then compare the VIZFP fingerprint lines across the presets. Writes nothing into the source tree.
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-viz"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-VIZ-001\qualification"
$summary = Join-Path $out "summary.txt"
"postrun-viz started $(Get-Date -Format o)" | Out-File $summary
"== layering checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
"== thermal visualization ownership checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckThermalVizOwnership.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
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
    'unit\.ThermalViz',
    'unit\.ThermalViz_(ConsumesQualified(Gradient|HeatFlux|Temperature)|TemperatureRemainsKelvin|GradientRemainsKelvinPerMetre|HeatFluxRemainsWattPerSquareMetre|SIMetreToOCCTMillimetre|AnOverlayInMetresCoincides|EverythingLeavingThisLayerIsSiMetres|ChannelsDescribeThemselves|TheGradientMagnitudeIsTheNorm)',
    'unit\.ThermalViz_(TemperatureContourUsesNodalValues|GradientMagnitudeContourUsesElementResult|HeatFluxMagnitudeContourUsesElementResult|ATemperatureContourReachesTheScreen|ConstantScalarRangeHandled|ManualRangeClamps|RejectsAnUnusableDisplayRange|TheBandsPartition|TheLegendNames|TheSameScalarGetsTheSameColour|ARangeTooNarrow|ALegendOfAnotherField|APartialFieldIsNeverColoured|AnElementChannelNeeds|ColourBuffersAreDeterministic|TheContourCacheIsKeyed|AnEmptyDomain)',
    'unit\.ThermalViz_(HeatFluxVectorDirection|ZeroHeatFluxVector|VectorScalingDoesNotMutateResult|TheDisplayThresholdIsReadability|RecoveredHeatFluxFlowsFromHotToCold|RecoveredQAndAppliedQ|AnOverflowingArrowLength|VectorGlyphsCarryTheOrdinal)',
    'unit\.ThermalViz_(PositiveAppliedHeatFluxArrow|NegativeAppliedHeatFluxArrow|AZeroAppliedFlux|PrescribedTemperatureSymbol|ConvectionSymbol|BoundarySetHighlight|MultipleBoundaries|IdenticalPrescriptions|NoInternalFace|OutwardNormalMatches|ACurvedWall|AnUnresolvedReference|AGlyphIsRefused)',
    'unit\.ThermalViz_(ClassifierCardinalityMatchesExpected|ZeroClassifiedEntitiesRejected|PartialClassificationNotReportedComplete|RejectsTemperatureCardinalityMismatch|RejectsElementGradientCardinalityMismatch|RejectsHeatFluxCardinalityMismatch|RejectsANonFiniteValue|RejectsAStateThatIsNotStored|IncompleteTargetsAreCounted)',
    'unit\.ThermalViz_(ExtremaTieBreakDeterministic|HighestTemperatureNavigation|LowestTemperatureNavigation|HighestHeatFluxNavigation|NavigationLooksAtTheEntity|TheLowestValueTie)',
    'unit\.ThermalViz_(NodeTemperatureInspection|ElementThermalInspection|SparseNodeIdInspection|SparseElementIdInspection|PickMapsARenderTriangle)',
    'unit\.ThermalViz_(CurrentResultDisplaysNormally|StaleResultHasExplicitState|SameCountsNewMeshStillStale|SameIdsNewMeshStillStale|FailedRecompute|DoesNotMapStaleResultToNewMesh|StaleResultSuppresses|TheStatesBeforeAnyResult|TheValidationBadge|AWarningStaysDrawn|ARecoveredResultIsBound|AViewBuiltForOneResult)',
    'unit\.ThermalViz_(ASteadyResultIsOneFrame|TransientCacheInvalidates|TransientFrameBinds|TransientFramesDraw|TransientPlaybackDoesNotInterpolate|TransientPlaybackUsesStoredTimes)',
    'unit\.ThermalViz_(DoesNotMutateMeshDuringUnitConversion|DoesNotMutateThermalResult|ViewStateChangesDoNotStale|ViewerStateChangesDoNotStale)',
    'unit\.ThermalViz_(LargeCurvedMeshVisualizationDataSmoke|LargeMeshVisualizationDataSmoke|LargeMeshVisualizationReachesTheScreen|ThermalOverlaysReachTheScreen)',
    'unit\.ThermalViz_(DisplayStateAndInspectionAreDeterministic|FieldExtractionIsDeterministic|VisualizationDataIsDeterministicAndFingerprinted)',
    'compile_fail\.thermalviz\.', 'architecture\.thermalviz-ownership',
    'unit\.ThermalValidation', 'architecture\.validation-ownership', 'compile_fail\.thermalvalidation\.',
    'unit\.StructuralViz', 'unit\.Structural', 'unit\.View', 'unit\.Mesh',
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
    foreach ($dll in "libbettercad_renderer.dll", "libbettercad_thermal.dll") {
        if (Test-Path (Join-Path $bin $dll)) {
            $f = Get-Item (Join-Path $bin $dll)
            "  $dll $($f.Length) " + (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower() | Out-File $summary -Append
        }
    }
    if (Test-Path (Join-Path $bin "libbettercad_renderer.dll")) {
        "  dll count: " + (Get-ChildItem $bin -Filter "libbettercad_*.dll" | Measure-Object).Count | Out-File $summary -Append
    }
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    "  ctest -N: $n" | Out-File $summary -Append
    foreach ($filter in $filters) {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    # THE VISUALIZATION SUITE through the XML reporter: the console reporter wraps a long line in the middle of a token.
    $xmlFile = Join-Path $out "viz-$preset.xml"
    & (Join-Path $bin "bettercad_tests.exe") "ThermalViz_*" -r xml > $xmlFile 2>&1
    $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
    "  ThermalViz_* : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value | Out-File $summary -Append
    $lines = [regex]::Matches($xml, '(VIZFP) [^\r\n<]*') | ForEach-Object { $_.Value }
    $lines | Out-File (Join-Path $out "viz-messages-$preset.txt") -Encoding utf8
    "  evidence lines: $($lines.Count)" | Out-File $summary -Append
    foreach ($suite in @(@("ThermalValidation_*", "validation"), @("ThermalBalance_*", "balance"), @("ThermalPost_*", "post"), @("ThermalTransient_*", "transient"), @("StructuralViz_*", "structviz"), @("[structural]", "structural"), @("[meshing]", "meshing"))) {
        $file = Join-Path $out "$($suite[1])-$preset.txt"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] > $file 2>&1
        "  $($suite[0]) exit $LASTEXITCODE : " + ((Get-Content $file | Select-String "^test cases:|^assertions:|^All tests passed").Line -join " | ") | Out-File $summary -Append
    }
}
"== cross-preset classification (every VIZFP line of the visualization suite)" | Out-File $summary -Append
$ref = Get-Content (Join-Path $out "viz-messages-debug-ext.txt") | Where-Object { $_ -match "^VIZFP " } | Sort-Object
"  debug-ext VIZFP lines: $($ref.Count)" | Out-File $summary -Append
foreach ($l in $ref) { "    $l" | Out-File $summary -Append }
foreach ($preset in "release-ext", "debug-shared-ext") {
    $other = Get-Content (Join-Path $out "viz-messages-$preset.txt") | Where-Object { $_ -match "^VIZFP " } | Sort-Object
    $diff = Compare-Object $ref $other
    "  VIZFP lines, debug-ext vs $preset : $($ref.Count) vs $($other.Count), differing: $(($diff | Measure-Object).Count)" | Out-File $summary -Append
    $diff | Select-Object -First 8 | ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" } | Out-File $summary -Append
}
"postrun-viz finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
