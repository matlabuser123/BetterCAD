# After P18-VALID-001's three-preset qualification: measure each preset's own binaries and run the validation suite (and its neighbours) on each.
# Writes nothing into the source tree.
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-valid"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-VALID-001\qualification"
$summary = Join-Path $out "summary.txt"
"postrun-valid started $(Get-Date -Format o)" | Out-File $summary
"== layering checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
"== validation ownership checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckValidationOwnership.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
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
$filters = @('unit\.ThermalValidation', 'unit\.ThermalValidation_(PolicyIsAccepted|PolicyConstantsArePinned|PolicyFingerprint|SeverityIsAFunction|EveryRuleHasAStableId)', 'unit\.ThermalValidation_(StatusAndCountsAreDerived|ASkippedRuleIsNeverAPass|SolveAcceptedIsNot|ModeSpecificChecks)', 'unit\.ThermalValidation_(RejectsAnInvertedTetrahedron|RejectsADegenerateTetrahedron|ANotANumberMetricIsInvalid|AcceptsAWellShaped|ThermalMeshQualityWarning|ThermalMeshQualityReject$|ReportsTheNumberOfElements|ComparesStrictly|WarnsAboutAContradictory|ReusesP16Metrics|RejectionByThermalPolicy)', 'unit\.ThermalValidation_(ThermalMeshQualityRejectOnARealMesh|TheAspectRatioBoundCatches|QualifiedReferenceMeshesAreAccepted|AcceptsEveryQualifiedReferenceMesh|TheEnvelopeBoundsAreTheMeasuredWorstCase|RejectsAMeshWhoseSizingIntent|RejectsMissingMesh|RejectsStaleMesh|AWarningOnly)', 'unit\.ThermalValidation_(MeasuresHow|TheBoundsGuarantee)', 'unit\.ThermalValidation_(SteadyRequiresConductivityOnly|TransientRequiresConductivityDensitySpecificHeat|RejectsMissingMaterial|RejectsAMaterialWithoutAConductivity|NonFiniteAndNonPositive)', 'unit\.ThermalValidation_(RejectsUnresolved|NoLoadsIsNot|AZeroMagnitudeLoad|RejectsConflicting|IdenticalDuplicate|InvalidConvection)', 'unit\.ThermalValidation_(RejectsSameCounts|RejectsSameIds|RejectsMismatchedAssembly|EveryEditStales|EveryTransientEditStales|InputEditStales|FailedResolve|ANewSolveThatFails|RejectsAMissingAnalysis)', 'unit\.ThermalValidation_(SteadyPureNeumann|SteadyDirichlet|SteadyRobin|FullyPrescribedSteady|FullyConstrainedSteadyPasses|DisconnectedUnanchored|DisconnectedAllAnchored|TransientPureNeumann|TransientDisconnected|TransientMissingCapacity|TransientCapacityMust|TheValidatorAndTheSolverAgree)', 'unit\.ThermalValidation_(AcceptsQualifiedSteadyResidual|RejectsBadSteadyResidual|SolverSuccessDoesNotOverride|ResidualIsJudgedAgainst|ANotANumberResidual|RejectsAPrescribedTemperatureThatIsNotHeld|RejectsAPivotRatio|RejectsInvalidSolverSettings|FullyConstrainedSteadyResidual|ASteadyFieldOfTheWrongSize|AcceptsQualifiedTransientResiduals|RejectsAnyBadTransientStep|TheFinalPartialStep|FinalOnlyHistoriesAreJudged|ASingleStepFinalOnlyRun|ANotANumberTransientRecord|ATransientHistoryWithTheWrongNumber|TransientResidualsUseTheCanonical)', 'unit\.ThermalValidation_(AcceptsQualifiedSteadyBalance|RejectsBadSteadyBalance|BalanceIsJudgedByBalances|ABalanceThatRecordsItself|RejectsNonFiniteBalance|ABalanceWithTheWrongNumber|EveryStepOfATransientBalance|BalancePassDoesNotOverride|ResidualPassDoesNotOverride)', 'unit\.ThermalValidation_(RejectsNonFiniteTemperature|RejectsATemperatureBelow|RejectsAStateOfTheWrongSize|EveryNonFiniteState|AcceptsAFiniteComplete|RejectsNonFiniteGradient|RejectsNonFiniteHeatFlux|RejectsAMissingElementResult|RejectsRecoveredTemperatures|RejectsARecoveredResultWithTheWrong)', 'unit\.ThermalValidation_(AcceptsValidTransientSetup|RejectsInvalidInitialTemperature|RejectsInvalidTimeSettings|RejectsTimeSettingsPastTheResourceGuard|AFinalOnlyHistoryOfSeveralSteps|ASingleStepFinalOnlyTransientIsFully)', 'unit\.ThermalValidation_(ValidSteadyAnalysisPasses|ValidTransientAnalysisPasses|ValidationIsReadOnly|ReportOrderingAndContents|RepeatedReportsOfEveryClass|PrimaryFailureReason)', 'unit\.ThermalValidation_(EvidenceOfAQualified|APerturbedFreeTemperature|ACorruptedBalanceFailsTheBalanceAlone|ANotANumberTemperatureFailsTheFinitenessGate|ANotANumberHeatFluxFailsThePostGate|ABalanceThatCancelsDoes|AResidualThatPassesDoesNotRescue|ABalanceMissingACategory|ATransientHistoryWithAPerturbedMiddleState|ATransientHistoryWithAWrongStateCount|AnAbsentSolutionOrRecordedFailure)', 'compile_fail\.thermalvalidation\.', 'architecture\.validation-ownership', 'unit\.ThermalBalance', 'compile_fail\.thermalbalance\.', 'architecture\.balance-ownership', 'unit\.ThermalPost', 'compile_fail\.thermalpost\.', 'architecture\.recovery-ownership', 'unit\.ThermalTransient', 'compile_fail\.thermaltransient\.', 'unit\.ThermalSolve', 'unit\.Linalg_', 'compile_fail\.thermalsolve\.', 'architecture\.targets\.', 'architecture\.checker\.linalg', 'unit\.ThermalAssembly', 'compile_fail\.thermalsys\.', 'unit\.ThermalBC', 'unit\.ThermalLoad', 'unit\.ThermalElement', 'unit\.ThermalDof', 'unit\.ThermalMaterial', 'unit\.ThermalData', 'unit\.Thermal', 'unit\.Structural', 'unit\.Tet4Element', 'compile_fail\.structsolve', '^architecture\.')
foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
    $bin = "$root\$preset\bin"
    "== $preset" | Out-File $summary -Append
    foreach ($exe in "bettercad-cli.exe", "bettercad_example_reference_models.exe", "bettercad_tests.exe") {
        $f = Get-Item (Join-Path $bin $exe)
        $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        "  $exe $($f.Length) $h" | Out-File $summary -Append
    }
    if (Test-Path (Join-Path $bin "libbettercad_thermal.dll")) {
        $f = Get-Item (Join-Path $bin "libbettercad_thermal.dll")
        "  libbettercad_thermal.dll $($f.Length) " + (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower() | Out-File $summary -Append
        "  dll count: " + (Get-ChildItem $bin -Filter "libbettercad_*.dll" | Measure-Object).Count | Out-File $summary -Append
    }
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    "  ctest -N: $n" | Out-File $summary -Append
    foreach ($filter in $filters) {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    # THE VALIDATION SUITE through the XML reporter: the console reporter wraps a long line in the middle of a token.
    $xmlFile = Join-Path $out "validation-$preset.xml"
    & (Join-Path $bin "bettercad_tests.exe") "ThermalValidation_*" -r xml > $xmlFile 2>&1
    $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
    "  ThermalValidation_* : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value | Out-File $summary -Append
    $lines = [regex]::Matches($xml, '(REPORT|FINDING|OVERALL|METRICS|MATRIX|FP|CANCELLATION|LAW|SWEEP|ENVELOPE) [^\r\n<]*') | ForEach-Object { $_.Value }
    $lines | Out-File (Join-Path $out "validation-messages-$preset.txt") -Encoding utf8
    "  evidence lines: $($lines.Count)" | Out-File $summary -Append
    foreach ($suite in @(@("ThermalBalance_*", "balance"), @("ThermalPost_*", "post"), @("ThermalTransient_*", "transient"), @("ThermalSolve_*", "solve"), @("Linalg_*", "linalg"), @("ThermalAssembly_*", "as"), @("ThermalBC_*", "bc"), @("ThermalLoad_*", "load"), @("ThermalElement_*", "elem"), @("[thermal][dof]", "dof"), @("[thermal][material]", "material"), @("[thermal][data]", "data"), @("[structural]", "structural"), @("[meshing]", "meshing"))) {
        $file = Join-Path $out "$($suite[1])-$preset.txt"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] > $file 2>&1
        "  $($suite[0]) exit $LASTEXITCODE : " + ((Get-Content $file | Select-String "^test cases:|^assertions:|^All tests passed").Line -join " | ") | Out-File $summary -Append
    }
}
"== cross-preset classification (every FP, OVERALL and REPORT line of the validation suite)" | Out-File $summary -Append
foreach ($kind in 'FP', 'OVERALL', 'REPORT', 'FINDING', 'MATRIX') {
    $ref = Get-Content (Join-Path $out "validation-messages-debug-ext.txt") | Where-Object { $_ -match "^$kind " } | Sort-Object
    foreach ($preset in "release-ext", "debug-shared-ext") {
        $other = Get-Content (Join-Path $out "validation-messages-$preset.txt") | Where-Object { $_ -match "^$kind " } | Sort-Object
        $diff = Compare-Object $ref $other
        "  $kind lines, debug-ext vs $preset : $($ref.Count) vs $($other.Count), differing: $(($diff | Measure-Object).Count)" | Out-File $summary -Append
        $diff | Select-Object -First 8 | ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" } | Out-File $summary -Append
    }
}
"== cross-preset bit fingerprints of the neighbours (balance)" | Out-File $summary -Append
$bal = @{}
foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
    $joined = ((Get-Content (Join-Path $out "balance-$preset.txt")) -join " ") -replace '\s+', ' '
    $bal[$preset] = [regex]::Matches($joined, 'bit fingerprint[^:]*: ([0-9a-f]{16})') | ForEach-Object { $_.Groups[1].Value }
    "  $preset : " + ($bal[$preset] -join " ") | Out-File $summary -Append
}
"  balance fingerprints identical across presets: " + ((@($bal.Values | ForEach-Object { $_ -join ',' } | Sort-Object -Unique).Count) -eq 1) | Out-File $summary -Append
"postrun-valid finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
