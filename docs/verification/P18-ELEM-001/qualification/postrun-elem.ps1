# After P18-ELEM-001's three-preset qualification: measure each preset's own
# binaries and run the element suite (and its neighbours) on each. Writes
# nothing into the source tree.
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-elem"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-ELEM-001\qualification"
$summary = Join-Path $out "summary.txt"
"postrun-elem started $(Get-Date -Format o)" | Out-File $summary
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
    foreach ($filter in 'unit\.ThermalElement', 'unit\.ThermalElement_(UnitTet|GeneralTet|ShapeGradient|Affine|Constant)', 'unit\.ThermalElement_(Inverted|Coplanar|Coincident|NearDegenerate|NonFinite)', 'unit\.ThermalElement_Conduct', 'unit\.ThermalElement_(Capacity|OnePoint|Density|SpecificHeat)', 'unit\.ThermalElement_(Translation|Rotation|Gradient|Reflection|Even|Odd)', 'unit\.ThermalElement_(GeometryScaling|ConductivityScaling)', 'unit\.ThermalElement_(Repeat|NoHidden|BitFingerprint)', 'compile_fail\.thermalelem\.', 'unit\.ThermalDof', 'unit\.ThermalMaterial', 'unit\.ThermalData', 'unit\.Thermal', 'unit\.Tet4Element', 'compile_fail\.thermaldof\.', '^architecture\.') {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    foreach ($suite in @(@("ThermalElement_*", "element"), @("[thermal][dof]", "dof"), @("[thermal][material]", "material"), @("[thermal][data]", "data"), @("[structural][elem]", "structural-elem"))) {
        $file = Join-Path $out "$($suite[1])-$preset.txt"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] > $file 2>&1
        "  $($suite[0]) exit $LASTEXITCODE : " + ((Get-Content $file | Select-String "^test cases:|^assertions:|^All tests passed").Line -join " | ") | Out-File $summary -Append
        $warns = Get-Content $file | Where-Object { $_ -match '^\s{2}\S' -and $_ -notmatch 'Randomness' }
        $warns | Out-File (Join-Path $out "$($suite[1])-messages-$preset.txt")
        "  message lines: $($warns.Count)" | Out-File $summary -Append
    }
}
"== cross-preset bit fingerprint (V, det J, G, Ke, Ce of 14 fixtures)" | Out-File $summary -Append
$prints = @{}
foreach ($preset in "debug-ext", "release-ext", "debug-shared-ext") {
    $line = (Get-Content (Join-Path $out "element-$preset.txt") | Where-Object { $_ -match "bit fingerprint" }) -join " "
    $prints[$preset] = if ($line -match ":\s*([0-9a-f]{16})") { $Matches[1] } else { "MISSING" }
    "  $preset : $($prints[$preset])" | Out-File $summary -Append
}
$distinct = ($prints.Values | Sort-Object -Unique).Count
"  distinct fingerprints: $distinct  ($(if ($distinct -eq 1) { 'BITWISE IDENTICAL across the three presets' } else { 'DIFFER across presets' }))" | Out-File $summary -Append
"postrun-elem finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
