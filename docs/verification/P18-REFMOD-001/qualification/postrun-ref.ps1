# After P18-REFMOD-001's three-preset qualification: read each preset's logs, measure each preset's OWN binaries, run the reference suite on each preset's
# test executable, read the workflow ledger and payloads each preset left in its build tree, and compare the presets with one another.
# Writes nothing into the source tree except the evidence copy it is asked to make (-CopyEvidence). ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([switch]$CopyEvidence)
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-ref"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-REFMOD-001\qualification"
$summary = Join-Path $out "summary.txt"
$presets = "debug-ext", "release-ext", "debug-shared-ext"
"postrun-ref started $(Get-Date -Format o)" | Out-File $summary
"== layering checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
"== warnings and errors in the harness build logs" | Out-File $summary -Append
foreach ($preset in $presets) {
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
$filters = @(
    'unit\.ThermalReference_', 'unit\.ThermalReference_RM01', 'unit\.ThermalReference_RM02', 'unit\.ThermalReference_RM03', 'unit\.ThermalReference_RM04',
    'unit\.ThermalReference_RM05', 'unit\.ThermalReference_RM06', 'unit\.ThermalReference_RM07', 'unit\.ThermalReference_RM08', 'unit\.ThermalReference_RM09',
    'unit\.ThermalReference_RM10', 'unit\.ThermalReference_Persistence', 'unit\.ThermalReference_Determinism', 'unit\.ThermalReference_Independence',
    'unit\.ThermalReference_Intent', 'refmod\.thermal\.', 'refmod\.thermal\.workflow$', 'refmod\.', 'unit\.Thermal', 'cli\.thermal\.', '^architecture\.'
)
$fresh = @{}
foreach ($preset in $presets) {
    $bin = "$root\$preset\bin"
    "== $preset" | Out-File $summary -Append
    foreach ($exe in "bettercad-cli.exe", "bettercad_tests.exe", "bettercad_example_reference_models.exe") {
        $f = Get-Item (Join-Path $bin $exe)
        $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        "  $exe $($f.Length) $h  $($f.FullName)" | Out-File $summary -Append
    }
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    "  ctest -N: $n" | Out-File $summary -Append
    foreach ($filter in $filters) {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    # THE REFERENCE SUITE through the XML reporter, on THIS preset's own test executable.
    $xmlFile = Join-Path $out "refmod-$preset.xml"
    & (Join-Path $bin "bettercad_tests.exe") "[refmod][thermal]" -r xml > $xmlFile 2>&1
    $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
    "  [refmod][thermal] : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value | Out-File $summary -Append
    # THE WORKFLOW'S LEDGER from this preset's own build tree.
    $wf = "$root\$preset\tests\cli-output\thermal-refmod-workflow"
    $ledger = Get-Content "$wf\ledger.txt"
    "  workflow ledger lines: $($ledger.Count)" | Out-File $summary -Append
    foreach ($l in ($ledger | Where-Object { $_ -match '^(cli|runner|tests) (binary|size|sha256|project-libraries)' })) { "    $l" | Out-File $summary -Append }
    $fresh[$preset] = $ledger
    "  runtime libraries resolved: " + (($ledger | Where-Object { $_ -match ' runtime ' } | Measure-Object).Count) | Out-File $summary -Append
    "  " + ($ledger | Where-Object { $_ -match '^processes ' }) | Out-File $summary -Append
    "  numeric comparison: " + (($ledger | Where-Object { $_ -match 'numeric comparison|All tests passed' }) -join ' ') | Out-File $summary -Append
    "  strict re-parse: " + (($ledger | Where-Object { $_ -match 'strict re-parse' }) -join ' ') | Out-File $summary -Append
}

# ---- THE PRESETS AGAINST ONE ANOTHER
"== cross-preset: the workflow's processes (exit code, byte counts, stdout hash) with each preset's build root removed; the runner's own line is excluded (it prints timings)" | Out-File $summary -Append
function Normalise([string]$line, [string]$preset) { return $line.Replace("C:/Users/hasib/bcdev/bc-build/$preset", "<build>") }
$tables = @{}
foreach ($preset in $presets) {
    $rows = @()
    foreach ($l in $fresh[$preset]) { if ($l -match '^[a-z0-9_.-]+ \| \d+ \| \d+ \|' -and $l -notmatch '^runner \|') { $rows += (Normalise $l $preset) } }
    $tables[$preset] = $rows
}
$ref = $tables["debug-ext"]
"  processes (excluding the runner): " + (($presets | ForEach-Object { "$_ $($tables[$_].Count)" }) -join "; ") | Out-File $summary -Append
foreach ($preset in "release-ext", "debug-shared-ext") {
    $same = 0; $differ = @(); $exitDiffer = 0
    for ($i = 0; $i -lt $ref.Count; $i++) {
        if ($ref[$i] -eq $tables[$preset][$i]) { $same++ } else {
            $differ += $i
            $a = $ref[$i] -split ' \| '; $b = $tables[$preset][$i] -split ' \| '
            if ($a[1] -ne $b[1]) { $exitDiffer++ }
        }
    }
    "  debug-ext vs ${preset}: $same of $($ref.Count) process lines identical; $($differ.Count) differ in bytes or hash; $exitDiffer differ in EXIT CODE" | Out-File $summary -Append
    foreach ($i in ($differ | Select-Object -First 6)) { "    #$($i + 1) debug-ext : $($ref[$i])`n         $preset : $($tables[$preset][$i])" | Out-File $summary -Append }
}
"== cross-preset: the payloads that differ are compared as NUMBERS (relative tolerance reported), schema and discrete fields exactly" | Out-File $summary -Append
function Flatten($node, [string]$prefix, $acc) {
    if ($node -is [System.Management.Automation.PSCustomObject]) { foreach ($p in $node.PSObject.Properties) { Flatten $p.Value "$prefix.$($p.Name)" $acc } }
    elseif ($node -is [System.Array]) { for ($i = 0; $i -lt $node.Count; $i++) { Flatten $node[$i] "$prefix[$i]" $acc } }
    else { $acc[$prefix] = $node }
}
foreach ($preset in "release-ext", "debug-shared-ext") {
    $compared = 0; $schemaDiff = 0; $discrete = 0; $numeric = 0; $worst = 0.0; $worstWhere = ""
    foreach ($f in (Get-ChildItem "$root\debug-ext\tests\cli-output\thermal-refmod-workflow\payloads" -Filter *.json | Where-Object Length -gt 0)) {
        $other = "$root\$preset\tests\cli-output\thermal-refmod-workflow\payloads\$($f.Name)"
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
    "  debug-ext vs ${preset}: $compared payloads compared; key sets differing: $schemaDiff; discrete/text fields differing: $discrete; numeric fields not bit-identical: $numeric; worst relative difference $worst $worstWhere" | Out-File $summary -Append
}
"== cross-preset: the 20 documents the runner wrote and the edited copy (SHA-256)" | Out-File $summary -Append
$docs = Get-ChildItem "$root\debug-ext\tests\cli-output\thermal-refmod-workflow\docs" -Filter *.bcad | Sort-Object Name
foreach ($d in $docs) {
    $hashes = @()
    foreach ($preset in $presets) { $hashes += (Get-FileHash "$root\$preset\tests\cli-output\thermal-refmod-workflow\docs\$($d.Name)" -Algorithm SHA256).Hash.ToLower() }
    "  {0,-44} {1}  {2}" -f $d.Name, $(if (($hashes | Select-Object -Unique).Count -eq 1) { "IDENTICAL in all three presets" } else { "DIFFERS" }), $hashes[0].Substring(0, 16) | Out-File $summary -Append
}
"== cross-preset: the evidence lines of the reference suite (BETTERCAD_REFMOD_EVIDENCE) with timing-free text compared" | Out-File $summary -Append
$env:BETTERCAD_REFMOD_EVIDENCE = "1"
$evidence = @{}
foreach ($preset in $presets) {
    $lines = & (Join-Path "$root\$preset\bin" "bettercad_tests.exe") "[refmod][thermal]" 2>&1 | Where-Object { $_ -match '^EVIDENCE ' } | Sort-Object
    $evidence[$preset] = $lines
    Set-Content -Path (Join-Path $out "evidence-$preset.txt") -Value $lines
    "  $preset : $($lines.Count) evidence lines" | Out-File $summary -Append
}
$env:BETTERCAD_REFMOD_EVIDENCE = $null
foreach ($preset in "release-ext", "debug-shared-ext") {
    $identical = 0; $differ = 0
    for ($i = 0; $i -lt [Math]::Min($evidence["debug-ext"].Count, $evidence[$preset].Count); $i++) {
        if ($evidence["debug-ext"][$i] -eq $evidence[$preset][$i]) { $identical++ } else { $differ++ }
    }
    "  debug-ext vs ${preset}: $identical evidence lines identical to the last digit, $differ differ" | Out-File $summary -Append
}
if ($CopyEvidence) {
    foreach ($preset in $presets) {
        $dest = "$q\workflow-$preset"
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        New-Item -ItemType Directory -Force $dest | Out-Null
        Copy-Item "$root\$preset\tests\cli-output\thermal-refmod-workflow\ledger.txt" $dest
        Copy-Item "$root\$preset\tests\cli-output\thermal-refmod-workflow\payloads" "$dest\payloads" -Recurse
        Copy-Item (Join-Path $out "evidence-$preset.txt") $dest
    }
    "evidence copied under $q\workflow-<preset>" | Out-File $summary -Append
}
"postrun-ref finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
