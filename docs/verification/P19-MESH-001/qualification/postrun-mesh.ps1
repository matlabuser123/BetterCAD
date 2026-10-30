# After P19-MESH-001's three-preset qualification: read each preset's logs, prove the binaries are fresh, build the ZERO-MATCH table (expected /
# discovered / executed per group, per preset), run the CFD boundary cases on each preset's OWN test executable, and compare the presets.
# Writes nothing into the source tree except the evidence copy it is asked to make (-CopyEvidence). ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([switch]$CopyEvidence)
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-mesh"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P19-MESH-001\qualification"
$summary = Join-Path $out "summary.txt"
$presets = "debug-ext", "release-ext", "debug-shared-ext"
$failures = 0
function Note([string]$text) { $text | Out-File $summary -Append -Width 4000 }
function Fail([string]$text) { $script:failures++; Note "  *** FAIL: $text" }
"postrun-mesh started $(Get-Date -Format o)" | Out-File $summary

Note "== layering and link-graph checkers on the source tree"
Note ((& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1))
Note ((& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckTargetDependencies.cmake" 2>&1 | Select-Object -Last 1))
Note "== warnings and errors in the harness build logs (0 unexpected warnings is a requirement)"
foreach ($preset in $presets) {
    $log = Join-Path $q "build-$preset.log"
    $w = (Select-String -Path $log -Pattern "warning:" | Measure-Object).Count
    $e = (Select-String -Path $log -Pattern "error:" | Measure-Object).Count
    $objects = (Select-String -Path $log -Pattern "Building CXX object" | Measure-Object).Count
    $cfdObjects = (Select-String -Path $log -Pattern "Building CXX object src/cfd/" | Measure-Object).Count
    Note "  $preset : warning: $w  error: $e  objects $objects  (cfd: $cfdObjects)"
    if ($w -ne 0 -or $e -ne 0) { Fail "$preset build log has $w warning(s) and $e error(s)" }
    $rebuild = (Get-Content (Join-Path $q "rebuild-$preset.log") | Select-String -Pattern "^\[" | Measure-Object).Count
    Note "  $preset no-op rebuild edges: $rebuild"
    if ($rebuild -gt 1) { Fail "$preset no-op rebuild did work ($rebuild edges)" }
    $ct = (Get-Content (Join-Path $q "ctest-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | "
    Note "  $preset ctest: $ct"
    if ($ct -notmatch "100% tests passed") { Fail "$preset ctest did not pass everything" }
}
foreach ($preset in "release-ext", "debug-ext") {
    $ct = (Get-Content (Join-Path $q "ctest-repeat-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | "
    Note "  repeat $preset : $ct"
    if ($ct -notmatch "100% tests passed") { Fail "$preset repeat did not pass everything" }
}
$errLines = @(Get-Content (Join-Path $q "run-qualification.err"))
$crlf = @($errLines | Where-Object { $_ -match "CRLF|LF will be replaced" }).Count
Note ("== harness stderr lines: " + $errLines.Count + " (git line-ending notices: " + $crlf + ")")
foreach ($l in ($errLines | Where-Object { $_ -notmatch "CRLF|LF will be replaced" -and $_.Trim() })) { Note "   other: $l" }
Note ("== qualification-times last line: " + (Get-Content (Join-Path $q "qualification-times.txt") | Select-Object -Last 1))

Note "== FRESH BINARY: each preset's binaries are newer than that preset's clean and newer than the newest file of the frozen source tree"
$sourceFiles = @()
foreach ($p in "apps", "include", "src", "tests", "examples", "cmake") { $sourceFiles += Get-ChildItem -File -Recurse (Join-Path $src $p) }
foreach ($p in "CMakeLists.txt", "CMakePresets.json") { $sourceFiles += Get-Item (Join-Path $src $p) }
$newest = $sourceFiles | Sort-Object LastWriteTime | Select-Object -Last 1
Note ("  newest source file: " + $newest.FullName.Substring($src.Length + 1) + "  " + $newest.LastWriteTime.ToString("o"))
$times = Get-Content (Join-Path $q "qualification-times.txt")
foreach ($preset in $presets) {
    $cleanLine = $times | Where-Object { $_ -match "^$preset clean attempt \d+ exit 0 " } | Select-Object -First 1
    # cmd's %time% pads a one-digit hour with a space, so the harness writes TWO spaces before "2:48:27.59": \s+, not one space.
    $m = [regex]::Match($cleanLine, 'exit 0 \w+ (\d+/\d+/\d+)\s+(\d+:\d+:\d+\.\d+)')
    if (-not $m.Success) { Fail "$preset clean time not found in qualification-times.txt"; continue }
    $cleanAt = [DateTime]::ParseExact("$($m.Groups[1].Value) $($m.Groups[2].Value)", "M/d/yyyy H:mm:ss.ff", [Globalization.CultureInfo]::InvariantCulture)
    Note ("  {0,-17} cleaned {1}" -f $preset, $cleanAt.ToString("yyyy-MM-dd HH:mm:ss.ff"))
    $binaries = @("bettercad-cli.exe", "bettercad_tests.exe")
    if ($preset -eq "debug-shared-ext") { $binaries += "libbettercad_cfd.dll" }
    foreach ($exe in $binaries) {
        $f = Get-Item "$root\$preset\bin\$exe"
        $hash = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        $afterClean = $f.LastWriteTime -gt $cleanAt
        $afterSource = $f.LastWriteTime -gt $newest.LastWriteTime
        Note ("  {0,-17} {1,-24} {2,11} bytes  built {3}  after clean {4}  after newest source {5}  sha256 {6}" -f $preset, $exe, $f.Length, $f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"), $afterClean, $afterSource, $hash)
        if (-not $afterClean -or -not $afterSource) { Fail "$preset $exe is not provably fresh" }
    }
}

Note "== ZERO-MATCH TABLE: for every group of the guard, per preset: EXPECTED (the guard's minimum), DISCOVERED (ctest -N -R, fixture setups excluded), EXECUTED-AND-PASSED (the full ctest log)"
$guard = Get-Content "$src\tests\cli\ZeroMatchGuard.cmake"
$groups = @()
foreach ($line in $guard) {
    $mm = [regex]::Match($line, '^\s*"(.+)"\s+(\d+)\s*$')
    if ($mm.Success) { $groups += [pscustomobject]@{ Pattern = $mm.Groups[1].Value.Replace('\\', '\'); Minimum = [int]$mm.Groups[2].Value } }
}
Note "  groups in the guard: $($groups.Count)"
$passedByPreset = @{}
foreach ($preset in $presets) {
    $names = @()
    foreach ($line in (Get-Content (Join-Path $q "ctest-$preset.log"))) {
        $t = [regex]::Match($line, 'Test\s+#\d+:\s+(.+?)\s+\.{2,}\s*(\*\*\*\w+|Passed|Failed)')
        if ($t.Success -and $t.Groups[2].Value -eq "Passed") { $names += $t.Groups[1].Value }
    }
    $passedByPreset[$preset] = $names
    Note "  $preset : $($names.Count) tests passed in the full ctest log"
}
$zeroFail = 0
foreach ($g in $groups) {
    $disc = @{}; $exec = @{}
    foreach ($preset in $presets) {
        $rx = [regex]::new($g.Pattern)
        $listed = & ctest --test-dir "$root\$preset" -N -R $g.Pattern 2>&1 | Select-String "Test\s+#\d+:\s+(.+)$" | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
        $disc[$preset] = @($listed | Where-Object { $rx.IsMatch($_) }).Count
        $exec[$preset] = @($passedByPreset[$preset] | Where-Object { $rx.IsMatch($_) }).Count
    }
    $ok = $true
    foreach ($preset in $presets) { if ($disc[$preset] -lt $g.Minimum -or $exec[$preset] -ne $disc[$preset]) { $ok = $false } }
    if (-not $ok) { $zeroFail++ }
    $label = if ($g.Pattern.Length -gt 100) { $g.Pattern.Substring(0, 97) + "..." } else { $g.Pattern }
    Note ("  {0,-102} min {1,4}  disc {2,4} {3,4} {4,4}  exec {5,4} {6,4} {7,4} {8}" -f $label, $g.Minimum, $disc["debug-ext"], $disc["release-ext"], $disc["debug-shared-ext"], $exec["debug-ext"], $exec["release-ext"], $exec["debug-shared-ext"], $(if ($ok) { "" } else { "<== MISMATCH" }))
}
Note "  groups with discovered < expected or executed != discovered: $zeroFail"
if ($zeroFail -ne 0) { Fail "$zeroFail zero-match group(s) are not fully discovered and executed" }
Note ("  an impossible filter discovers: " + ((& ctest --test-dir "$root\release-ext" -N -R "unit\.ThisTestDoesNotExist_P19MESH001" 2>&1 | Select-String "Total Tests:").Line))

Note "== THE CFD DATA, BOUNDARY, FLUID AND MESH CASES, on each preset's own executable; the compile-fail group through each preset's CTest"
$assertions = @{}
$golden = @{}
$meshText = @{}
foreach ($preset in $presets) {
    $log = Join-Path $out "cfdmesh-$preset.txt"
    & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "CfdData_*,CfdInput_*,CfdFluid_*,CfdMesh_*,FluidProperty_*" > $log 2>&1
    $tail = (Get-Content $log | Where-Object { $_.Trim() } | Select-Object -Last 1)
    Note "  $preset : $tail"
    if ($tail -notmatch "All tests passed") { Fail "$preset CfdData_*/CfdInput_*/CfdFluid_*/CfdMesh_* did not pass" }
    # The mesh cases alone, with their census and quality WARN lines: the text must be identical on every preset (the seed line excepted).
    $meshLog = Join-Path $out "cfdmesh-census-$preset.txt"
    & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "CfdMesh_*" > $meshLog 2>&1
    $meshText[$preset] = ((Get-Content $meshLog | Where-Object { $_ -notmatch "Randomness seeded" }) -join "`n")
    $census = @(Select-String -Path $meshLog -Pattern "RM-MESH-0\d").Count
    Note "  $preset CfdMesh_* census and quality lines: $census"
    if ($census -ne 16) { Fail "$preset CfdMesh_* printed $census RM-MESH lines, expected 16 (8 census + 8 quality)" }
    $assertions[$preset] = [regex]::Match($tail, '\((\d+) assertions in (\d+) test cases\)').Value
    $golden[$preset] = (Select-String -Path $log -Pattern "golden fingerprint (\S+)" | Select-Object -First 1).Matches[0].Groups[1].Value
    Note "  $preset golden fingerprint: $($golden[$preset])"
    $cf = & ctest --test-dir "$root\$preset" -R "compile_fail\.(cfdids|prov)\." 2>&1 | Select-String "tests passed|Total Test time"
    Note ("  $preset compile_fail.cfdids+prov: " + ($cf.Line -join " | "))
    if (($cf.Line -join " ") -notmatch "100% tests passed") { Fail "$preset compile_fail.cfdids+prov did not pass" }
}
if (($assertions.Values | Select-Object -Unique).Count -ne 1) { Fail "the CFD cases ran a different number of assertions on different presets" }
Note ("  assertions identical on all three presets: " + (($assertions.Values | Select-Object -Unique).Count -eq 1) + " (" + ($assertions.Values | Select-Object -First 1) + ")")
if (($golden.Values | Select-Object -Unique).Count -ne 1 -or -not ($golden.Values | Select-Object -First 1)) { Fail "the golden fingerprint differs between presets or is missing" }
Note ("  golden fingerprint identical on all three presets: " + (($golden.Values | Select-Object -Unique).Count -eq 1))
$meshSame = (($meshText.Values | Select-Object -Unique).Count -eq 1)
if (-not $meshSame) { Fail "the CfdMesh_* output (census and measured quality) differs between presets" }
Note ("  CfdMesh_* output (census and measured quality) identical on all three presets: " + $meshSame)
Note "== CROSS-PRESET: the bit-exact thermal/structural/reference suites still agree (the cases below are carried; the full agreement is in the full ctest of each preset)"
foreach ($preset in $presets) {
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    Note "  $preset : $($n.Line)"
}
if ($CopyEvidence) {
    foreach ($preset in $presets) {
        Copy-Item (Join-Path $out "cfdmesh-$preset.txt") "$q\cfdmesh-$preset.txt"
        Copy-Item (Join-Path $out "cfdmesh-census-$preset.txt") "$q\cfdmesh-census-$preset.txt"
    }
    Note "evidence copied under $q"
}
Note "postrun-mesh finished $(Get-Date -Format o) ; failures recorded by this script: $failures"
Get-Content $summary
exit $failures
