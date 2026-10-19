# After P18-BC-001's three-preset qualification: measure each preset's own
# binaries and run the boundary-condition suite (and its neighbours) on each. Writes
# nothing into the source tree.
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-bc"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-BC-001\qualification"
$summary = Join-Path $out "summary.txt"
"postrun-bc started $(Get-Date -Format o)" | Out-File $summary
"== layering checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
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
    foreach ($filter in 'unit\.ThermalLoad', 'unit\.ThermalBC', 'unit\.ThermalBC_(PrescribedTemperatureDef|ConvectionDef|CanonicalGeometry|UnitsContract|SignConvention)', 'unit\.ThermalBC_(PrescribedTemperature(Maps|Uses|Stores|Dedup|Deter)|MappedFaceDirichlet)', 'unit\.ThermalBC_(Duplicate|Conflict|PartialSurface|IdenticalOverlap)', 'unit\.ThermalBC_(SingleTriangle|GeneralTriangle|ConvectionMatrix|ConvectionAmbientEq|ConvectionAbove|ConvectionBelow|NonUniform|ConvectionFacet|SharedFacet|TriangleWinding|ConstantTemperature|MappedFaceConvection)', 'unit\.ThermalBC_(ConvectionRejects|ConvectionIsNotLumped|WrongSigns)', 'unit\.ThermalBC_(GeometryReference|PreparedBoundaryKeeps|DirichletAndRobin|NoDirichlet|Unresolved|Unsupported|NoNearest|OneInvalid|Empty)', 'unit\.ThermalBC_(PreparedBCBinds|Remesh|SameCounts|SameIds|ADifferentBody|Edit|Material|LoadSolver|GeometryEdit|DocumentBoundary)', 'unit\.ThermalBC_(Sparse|LargeSparse)', 'unit\.ThermalBC_(RepeatedPreparation|Diagnostics|BitFingerprint)', 'compile_fail\.thermalbc\.', 'unit\.ThermalLoad_(Surface|SingleTriangle|GeneralTriangle|Triangulation|FacetWinding)', 'unit\.ThermalLoad_(Volume|UnitTet|GeneralTet|Generation|SharedVolume|MultiTet)', 'unit\.ThermalLoad_(GeometryReference|Unresolved|Malformed|NoNearest|Empty|Target|OneInvalid)', 'unit\.ThermalLoad_(Prepared|Remesh|SameCounts|SameIds|Edit|Material|Solver|Geometry|Document)', 'unit\.ThermalLoad_(Sparse|LargeSparse)', 'unit\.ThermalLoad_(.*Superposition|SignedCancellation|LoadOrder)', 'unit\.ThermalLoad_(Rejects|Zero|Duplicate|Repeated|ANumbering|Every|NonFinite)', 'unit\.ThermalLoad_(RepeatedPreparation|Diagnostics|BitFingerprint)', 'compile_fail\.thermalload\.', 'unit\.ThermalElement', 'unit\.ThermalDof', 'unit\.ThermalMaterial', 'unit\.ThermalData', 'unit\.Thermal', 'unit\.Tet4Element', 'compile_fail\.thermalelem\.', 'compile_fail\.thermaldof\.', '^architecture\.') {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    foreach ($suite in @(@("ThermalBC_*", "bc"), @("ThermalLoad_*", "load"), @("ThermalElement_*", "element"), @("[thermal][dof]", "dof"), @("[thermal][material]", "material"), @("[thermal][data]", "data"), @("[structural][elem]", "structural-elem"))) {
        $file = Join-Path $out "$($suite[1])-$preset.txt"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] > $file 2>&1
        "  $($suite[0]) exit $LASTEXITCODE : " + ((Get-Content $file | Select-String "^test cases:|^assertions:|^All tests passed").Line -join " | ") | Out-File $summary -Append
        $warns = Get-Content $file | Where-Object { $_ -match '^\s{2}\S' -and $_ -notmatch 'Randomness' }
        $warns | Out-File (Join-Path $out "$($suite[1])-messages-$preset.txt")
        "  message lines: $($warns.Count)" | Out-File $summary -Append
    }
}
"== cross-preset bit fingerprints (hand cases; real block)" | Out-File $summary -Append
foreach ($which in @(@("hand Robin triangle", "hand Robin triangle:"), @("real block", "real block[^:]*:"))) {
    $prints = @{}
    foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
        $joined = (Get-Content (Join-Path $out "bc-$preset.txt")) -join " "; $line = if ($joined -match "(bit fingerprint, $($which[1])\s+[0-9a-f]{16})") { $Matches[1] } else { "" }
        $prints[$preset] = if ($line -match "([0-9a-f]{16})$") { $Matches[1] } else { "MISSING" }
        "  $($which[0]) $preset : $($prints[$preset])" | Out-File $summary -Append
    }
    $distinct = ($prints.Values | Sort-Object -Unique).Count
    "  $($which[0]) distinct fingerprints: $distinct  ($(if ($distinct -eq 1) { 'BITWISE IDENTICAL across the three presets' } else { 'DIFFER across presets' }))" | Out-File $summary -Append
}
"== cross-preset message lines of the load suite (every measurement the suite prints)" | Out-File $summary -Append
$ref = Get-Content (Join-Path $out "bc-messages-debug-ext.txt") | Sort-Object
foreach ($preset in "release-ext", "debug-shared-ext") {
    $other = Get-Content (Join-Path $out "bc-messages-$preset.txt") | Sort-Object
    $diff = Compare-Object $ref $other
    "  debug-ext vs $preset : $($ref.Count) lines vs $($other.Count) lines, differing: $(($diff | Measure-Object).Count)" | Out-File $summary -Append
    $diff | Select-Object -First 8 | ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" } | Out-File $summary -Append
}"postrun-bc finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
