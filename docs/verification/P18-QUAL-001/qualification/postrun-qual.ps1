# After P18-QUAL-001's three-preset qualification: read each preset's logs, prove the binaries are fresh, build the ZERO-MATCH table (expected /
# discovered / executed per group, per preset), run the reference-model matrix and the currentness matrix on each preset's OWN test executable, run
# the independent analytical verifier on each preset's fresh CLI payloads, read the workflow ledgers, and compare the presets with one another.
# Writes nothing into the source tree except the evidence copy it is asked to make (-CopyEvidence). ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([switch]$CopyEvidence)
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-qual"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-QUAL-001\qualification"
$summary = Join-Path $out "summary.txt"
$presets = "debug-ext", "release-ext", "debug-shared-ext"
$failures = 0
function Note([string]$text) { $text | Out-File $summary -Append -Width 4000 }
function Fail([string]$text) { $script:failures++; Note "  *** FAIL: $text" }
"postrun-qual started $(Get-Date -Format o)" | Out-File $summary

# ---------------------------------------------------------------- layering and the build logs
Note "== layering checker on the source tree"
Note ((& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1))
Note "== warnings and errors in the harness build logs (0 unexpected warnings is a requirement)"
foreach ($preset in $presets) {
    $log = Join-Path $q "build-$preset.log"
    $w = (Select-String -Path $log -Pattern "warning:" | Measure-Object).Count
    $e = (Select-String -Path $log -Pattern "error:" | Measure-Object).Count
    $objects = (Select-String -Path $log -Pattern "Building CXX object" | Measure-Object).Count
    $steps = (Select-String -Path $log -Pattern "^\[\d+/(\d+)\]" | Select-Object -Last 1).Line
    Note "  $preset : warning: $w  error: $e  objects $objects  last step $steps"
    if ($w -ne 0 -or $e -ne 0) { Fail "$preset build log has $w warning(s) and $e error(s)" }
    $rebuild = (Get-Content (Join-Path $q "rebuild-$preset.log") | Select-String -Pattern "^\[" | Measure-Object).Count
    Note "  $preset no-op rebuild edges: $rebuild"
    if ($rebuild -gt 1) { Fail "$preset no-op rebuild did work ($rebuild edges)" }
    Note ("  $preset clean: " + ((Get-Content (Join-Path $q "clean-$preset.log") | Select-String -Pattern "files").Line))
    $ct = (Get-Content (Join-Path $q "ctest-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | "
    Note "  $preset ctest: $ct"
    if ($ct -notmatch "100% tests passed") { Fail "$preset ctest did not pass everything" }
}
foreach ($preset in "release-ext", "debug-ext") {
    $ct = (Get-Content (Join-Path $q "ctest-repeat-$preset.log") | Select-String -Pattern "% tests passed|Total Test time").Line -join " | "
    Note "  repeat $preset : $ct"
    if ($ct -notmatch "100% tests passed") { Fail "$preset repeat did not pass everything" }
}
Note ("== harness stderr lines: " + (Get-Content (Join-Path $q "run-qualification.err") | Measure-Object -Line).Lines)
Note ("== qualification-times last line: " + (Get-Content (Join-Path $q "qualification-times.txt") | Select-Object -Last 1))

# ---------------------------------------------------------------- the fresh-binary proof (the freshness half; the workflows record identity)
Note "== FRESH BINARY: each preset's binaries are newer than that preset's clean and newer than the newest file of the frozen source tree"
$sourceFiles = @()
foreach ($p in "apps", "include", "src", "tests", "examples", "cmake") { $sourceFiles += Get-ChildItem -File -Recurse (Join-Path $src $p) }
foreach ($p in "CMakeLists.txt", "CMakePresets.json") { $sourceFiles += Get-Item (Join-Path $src $p) }
$newest = $sourceFiles | Sort-Object LastWriteTime | Select-Object -Last 1
Note ("  newest source file: " + $newest.FullName.Substring($src.Length + 1) + "  " + $newest.LastWriteTime.ToString("o"))
$times = Get-Content (Join-Path $q "qualification-times.txt")
foreach ($preset in $presets) {
    $cleanLine = $times | Where-Object { $_ -match "^$preset clean attempt \d+ exit 0 " } | Select-Object -First 1
    $m = [regex]::Match($cleanLine, 'exit 0 \w+ (\d+/\d+/\d+) (\d+:\d+:\d+\.\d+)')
    $cleanAt = [DateTime]::ParseExact("$($m.Groups[1].Value) $($m.Groups[2].Value)", "M/d/yyyy H:mm:ss.ff", [Globalization.CultureInfo]::InvariantCulture)
    foreach ($exe in "bettercad-cli.exe", "bettercad_tests.exe", "bettercad_example_reference_models.exe") {
        $f = Get-Item "$root\$preset\bin\$exe"
        $hash = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        $afterClean = $f.LastWriteTime -gt $cleanAt
        $afterSource = $f.LastWriteTime -gt $newest.LastWriteTime
        Note ("  {0,-17} {1,-38} {2,11} bytes  built {3}  after clean {4}  after newest source {5}  sha256 {6}" -f $preset, $exe, $f.Length, $f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"), $afterClean, $afterSource, $hash)
        if (-not $afterClean -or -not $afterSource) { Fail "$preset $exe is not provably fresh" }
    }
    Note ("  " + $preset + " clean at " + $cleanAt.ToString("yyyy-MM-dd HH:mm:ss"))
    Note ("  " + $preset + " cli version: " + ((& "$root\$preset\bin\bettercad-cli.exe" version 2>&1 | Out-String).Trim() -replace "\s+", " "))
}

# ---------------------------------------------------------------- the zero-match table
Note "== ZERO-MATCH TABLE: for every group of the guard, per preset: EXPECTED (the guard's minimum), DISCOVERED (ctest -N -R), EXECUTED-AND-PASSED (the full ctest log)"
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
$header = "  {0,-120} {1,8} {2,9} {3,9} {4,9} {5,9} {6,9} {7,9}" -f "group", "expected", "disc-dbg", "disc-rel", "disc-shr", "exec-dbg", "exec-rel", "exec-shr"
Note $header
$zeroFail = 0
foreach ($g in $groups) {
    $disc = @{}; $exec = @{}
    foreach ($preset in $presets) {
        $rx = [regex]::new($g.Pattern)
        # ctest -N -R also lists the FIXTURE SETUP tests a selected test requires (e.g. refmod.thermal.build); only the tests the filter itself selects are counted.
        $listed = & ctest --test-dir "$root\$preset" -N -R $g.Pattern 2>&1 | Select-String "Test\s+#\d+:\s+(.+)$" | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
        $disc[$preset] = @($listed | Where-Object { $rx.IsMatch($_) }).Count
        $exec[$preset] = @($passedByPreset[$preset] | Where-Object { $rx.IsMatch($_) }).Count
    }
    $ok = $true
    foreach ($preset in $presets) { if ($disc[$preset] -lt $g.Minimum -or $exec[$preset] -ne $disc[$preset]) { $ok = $false } }
    if (-not $ok) { $zeroFail++ }
    $label = if ($g.Pattern.Length -gt 118) { $g.Pattern.Substring(0, 115) + "..." } else { $g.Pattern }
    Note ("  {0,-120} {1,8} {2,9} {3,9} {4,9} {5,9} {6,9} {7,9} {8}" -f $label, $g.Minimum, $disc["debug-ext"], $disc["release-ext"], $disc["debug-shared-ext"], $exec["debug-ext"], $exec["release-ext"], $exec["debug-shared-ext"], $(if ($ok) { "" } else { "<== MISMATCH" }))
}
Note "  groups with discovered < expected or executed != discovered: $zeroFail"
if ($zeroFail -ne 0) { Fail "$zeroFail zero-match group(s) are not fully discovered and executed" }
Note ("  an impossible filter discovers: " + ((& ctest --test-dir "$root\release-ext" -N -R "unit\.ThisTestDoesNotExist_P18QUAL001" 2>&1 | Select-String "Total Tests:").Line))

# ---------------------------------------------------------------- the reference-model matrix and the currentness matrix, on each preset's own executable
Note "== REFERENCE-MODEL MATRIX (RM01-RM10 and the cross-cutting groups): [refmod][thermal] through the XML reporter on each preset's own test executable"
$rmGroups = "RM01", "RM02", "RM03", "RM04", "RM05", "RM06", "RM07", "RM08", "RM09", "RM10", "Persistence", "Determinism", "Independence", "Intent"
$matrix = @{}
foreach ($preset in $presets) {
    $xmlFile = Join-Path $out "refmod-$preset.xml"
    & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "[refmod][thermal]" -r xml > $xmlFile 2>&1
    $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
    Note ("  $preset : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value)
    $doc = [xml](Get-Content $xmlFile -Raw -ErrorAction SilentlyContinue)
    $rows = @{}
    foreach ($tc in $xml -split '(?=<TestCase )' | Where-Object { $_ -match '^<TestCase ' }) {
        $name = [regex]::Match($tc, 'name="([^"]*)"').Groups[1].Value
        $ok = [regex]::Match($tc, '<OverallResult success="(\w+)"').Groups[1].Value
        $rows[$name] = $ok
    }
    $matrix[$preset] = $rows
}
Note ("  {0,-16} {1,22} {2,22} {3,22}" -f "group", "debug-ext (cases ok/all)", "release-ext", "debug-shared-ext")
foreach ($rm in $rmGroups) {
    $cells = @()
    foreach ($preset in $presets) {
        $names = @($matrix[$preset].Keys | Where-Object { $_ -match "^ThermalReference_$rm" })
        $okc = @($names | Where-Object { $matrix[$preset][$_] -eq "true" }).Count
        $cells += ("{0}/{1}" -f $okc, $names.Count)
        if ($okc -ne $names.Count -or $names.Count -eq 0) { Fail "$preset $rm : $okc of $($names.Count)" }
    }
    Note ("  {0,-16} {1,22} {2,22} {3,22}" -f $rm, $cells[0], $cells[1], $cells[2])
}
Note "== CURRENTNESS MATRIX and the final-gate cases: [p18qual] on each preset's own executable (the WARN lines carry the sequence evidence)"
foreach ($preset in $presets) {
    $log = Join-Path $out "p18qual-$preset.txt"
    & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "[p18qual]" > $log 2>&1
    $tail = (Get-Content $log | Select-Object -Last 2) -join " | "
    Note "  $preset : $tail"
    if ($tail -notmatch "All tests passed") { Fail "$preset [p18qual] did not pass" }
}

# ---------------------------------------------------------------- the independent analytical verifier on each preset's fresh payloads
Note "== INDEPENDENT ANALYTICAL VERIFICATION of each preset's fresh CLI payloads (verify-independent.ps1; shares no code with BetterCAD)"
foreach ($preset in $presets) {
    $log = Join-Path $out "verify-$preset.txt"
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$q\verify-independent.ps1" -Preset $preset > $log 2>&1
    $code = $LASTEXITCODE
    Note ("  $preset : exit $code ; " + ((Get-Content $log | Select-String "checks failed").Line))
    if ($code -ne 0) { Fail "$preset independent verification failed ($code)" }
}
# negative control: the verifier must be able to fail
$tmp = Join-Path $out "verify-selftest"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Copy-Item "$root\release-ext\tests\cli-output\thermal-refmod-workflow\payloads" $tmp -Recurse
$p = Join-Path $tmp "rm05.balance.json"
$txt = Get-Content $p -Raw
$changed = [regex]::Replace($txt, '("dirichletReactionInto": \{\s*"value": )[0-9.eE+-]+', '${1}201.5', 1)
[IO.File]::WriteAllText($p, $changed)
& powershell -NoProfile -ExecutionPolicy Bypass -File "$q\verify-independent.ps1" -PayloadDir $tmp > (Join-Path $out "verify-selftest.txt") 2>&1
Note "  negative control (a doctored reaction of 201.5 W): verifier exit $LASTEXITCODE (must be non-zero)"
if ($LASTEXITCODE -eq 0) { Fail "the independent verifier cannot fail" }

# ---------------------------------------------------------------- the workflows' ledgers and the presets against one another
$workflows = "thermal-refmod-workflow", "thermal-workflow"
foreach ($wfName in $workflows) {
    Note "== WORKFLOW $wfName : the ledger each preset left in its own build tree"
    $fresh = @{}
    foreach ($preset in $presets) {
        $ledger = Get-Content "$root\$preset\tests\cli-output\$wfName\ledger.txt"
        $fresh[$preset] = $ledger
        Note "  $preset : ledger lines $($ledger.Count)"
        foreach ($l in ($ledger | Where-Object { $_ -match '^(binary|size|sha256|project-libraries|cli |runner |tests )' } | Select-Object -First 10)) { Note "    $l" }
    }
    function Normalise([string]$line, [string]$preset) { return $line.Replace("C:/Users/hasib/bcdev/bc-build/$preset", "<build>") }
    $tables = @{}
    foreach ($preset in $presets) {
        $rows = @()
        foreach ($l in $fresh[$preset]) { if ($l -match '^[A-Za-z0-9_.-]+ \| \d+ \| \d+ \|' -and $l -notmatch '^runner \|') { $rows += (Normalise $l $preset) } }
        $tables[$preset] = $rows
    }
    $ref = $tables["debug-ext"]
    Note ("  processes (excluding the runner): " + (($presets | ForEach-Object { "$_ $($tables[$_].Count)" }) -join "; "))
    foreach ($preset in "release-ext", "debug-shared-ext") {
        $same = 0; $differ = 0; $exitDiffer = 0
        for ($i = 0; $i -lt $ref.Count; $i++) {
            if ($ref[$i] -eq $tables[$preset][$i]) { $same++ } else {
                $differ++
                $a = $ref[$i] -split ' \| '; $b = $tables[$preset][$i] -split ' \| '
                if ($a[1] -ne $b[1]) { $exitDiffer++ }
            }
        }
        Note "  debug-ext vs ${preset}: $same of $($ref.Count) process lines identical; $differ differ in bytes or hash; $exitDiffer differ in EXIT CODE"
        if ($exitDiffer -ne 0) { Fail "$wfName $preset : $exitDiffer process(es) exited differently from debug-ext" }
    }
}
function Flatten($node, [string]$prefix, $acc) {
    if ($node -is [System.Management.Automation.PSCustomObject]) { foreach ($p in $node.PSObject.Properties) { Flatten $p.Value "$prefix.$($p.Name)" $acc } }
    elseif ($node -is [System.Array]) { for ($i = 0; $i -lt $node.Count; $i++) { Flatten $node[$i] "$prefix[$i]" $acc } }
    else { $acc[$prefix] = $node }
}
Note "== CROSS-PRESET: every payload both workflows wrote, as parsed values: key sets and text exactly; numbers by relative difference"
foreach ($wfName in $workflows) {
    foreach ($preset in "release-ext", "debug-shared-ext") {
        $compared = 0; $schemaDiff = 0; $discrete = 0; $numeric = 0; $worst = 0.0; $worstWhere = ""
        foreach ($f in (Get-ChildItem "$root\debug-ext\tests\cli-output\$wfName\payloads" -Filter *.json | Where-Object Length -gt 0)) {
            $other = "$root\$preset\tests\cli-output\$wfName\payloads\$($f.Name)"
            if (-not (Test-Path $other)) { $schemaDiff++; continue }
            $a = @{}; $b = @{}
            Flatten (Get-Content $f.FullName -Raw | ConvertFrom-Json) "" $a
            Flatten (Get-Content $other -Raw | ConvertFrom-Json) "" $b
            $compared++
            if ((($a.Keys | Sort-Object) -join ',') -ne (($b.Keys | Sort-Object) -join ',')) { $schemaDiff++; continue }
            foreach ($k in $a.Keys) {
                $x = $a[$k]; $y = $b[$k]
                if ($x -is [double] -or $x -is [decimal] -or $x -is [int] -or $x -is [long]) {
                    if ($x -ne $y) {
                        $numeric++
                        $rel = [math]::Abs([double]$x - [double]$y) / [math]::Max([math]::Abs([double]$x), 1e-300)
                        if ($rel -gt $worst) { $worst = $rel; $worstWhere = "$($f.Name)$k" }
                    }
                } elseif ($x -ne $y) { $discrete++ }
            }
        }
        Note "  $wfName debug-ext vs ${preset}: $compared payloads compared; key sets differing: $schemaDiff; discrete/text fields differing: $discrete; numeric fields not bit-identical: $numeric; worst relative difference $worst $worstWhere"
        if ($schemaDiff -ne 0 -or $discrete -ne 0) { Fail "$wfName $preset payload schema or text differs from debug-ext" }
    }
}
Note "== CROSS-PRESET: the committed reference documents the runner wrote (SHA-256)"
$docs = Get-ChildItem "$root\debug-ext\tests\cli-output\thermal-refmod-workflow\docs" -Filter *.bcad | Sort-Object Name
foreach ($d in $docs) {
    $hashes = @()
    foreach ($preset in $presets) { $hashes += (Get-FileHash "$root\$preset\tests\cli-output\thermal-refmod-workflow\docs\$($d.Name)" -Algorithm SHA256).Hash.ToLower() }
    $same = ($hashes | Select-Object -Unique).Count -eq 1
    Note ("  {0,-44} {1}  {2}" -f $d.Name, $(if ($same) { "IDENTICAL in all three presets" } else { "DIFFERS" }), $hashes[0].Substring(0, 16))
    if (-not $same) { Fail "$($d.Name) differs across presets" }
}
Note "== CROSS-PRESET: the evidence lines of the reference suite (BETTERCAD_REFMOD_EVIDENCE), timing-free"
$env:BETTERCAD_REFMOD_EVIDENCE = "1"
$evidence = @{}
foreach ($preset in $presets) {
    $lines = & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "[refmod][thermal]" 2>&1 | Where-Object { $_ -match '^EVIDENCE ' } | Sort-Object
    $evidence[$preset] = $lines
    Set-Content -Path (Join-Path $out "evidence-$preset.txt") -Value $lines
    Note "  $preset : $($lines.Count) evidence lines"
}
$env:BETTERCAD_REFMOD_EVIDENCE = $null
foreach ($preset in "release-ext", "debug-shared-ext") {
    $identical = 0; $differ = 0
    for ($i = 0; $i -lt [Math]::Min($evidence["debug-ext"].Count, $evidence[$preset].Count); $i++) {
        if ($evidence["debug-ext"][$i] -eq $evidence[$preset][$i]) { $identical++ } else { $differ++ }
    }
    Note "  debug-ext vs ${preset}: $identical evidence lines identical to the last digit, $differ differ"
}
if ($CopyEvidence) {
    foreach ($preset in $presets) {
        foreach ($wfName in $workflows) {
            $dest = "$q\$wfName-$preset"
            if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
            New-Item -ItemType Directory -Force $dest | Out-Null
            Copy-Item "$root\$preset\tests\cli-output\$wfName\ledger.txt" $dest
            Copy-Item "$root\$preset\tests\cli-output\$wfName\payloads" "$dest\payloads" -Recurse
        }
        Copy-Item (Join-Path $out "evidence-$preset.txt") "$q\evidence-$preset.txt"
        Copy-Item (Join-Path $out "p18qual-$preset.txt") "$q\p18qual-$preset.txt"
        Copy-Item (Join-Path $out "verify-$preset.txt") "$q\verify-$preset.txt"
    }
    Copy-Item (Join-Path $out "verify-selftest.txt") "$q\verify-selftest.txt"
    Note "evidence copied under $q"
}
Note "postrun-qual finished $(Get-Date -Format o) ; failures recorded by this script: $failures"
Get-Content $summary
exit $failures
