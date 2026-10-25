# After P18-CLI-001's three-preset qualification: measure each preset's OWN binaries, read the ledger and payloads the scripted workflow left in each
# preset's build tree, run the CLI suites on each preset's test executable, and compare the presets with one another. Writes nothing into the source tree
# except the evidence copy it is asked to make (-CopyEvidence).
param([switch]$CopyEvidence)
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$out = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-cli"
New-Item -ItemType Directory -Force $out | Out-Null
$root = "C:\Users\hasib\bcdev\bc-build"
$src = "C:\Users\hasib\OneDrive\Desktop\CAD"
$q = "$src\docs\verification\P18-CLI-001\qualification"
$summary = Join-Path $out "summary.txt"
$presets = "debug-ext", "release-ext", "debug-shared-ext"
"postrun-cli started $(Get-Date -Format o)" | Out-File $summary
"== layering checker and the CLI ownership checker on the source tree" | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckLayering.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
(& cmake "-DSOURCE_DIR=$src" -P "$src\tests\architecture\CheckThermalCliOwnership.cmake" 2>&1 | Select-Object -Last 1) | Out-File $summary -Append
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
    'unit\.ThermalCli_', 'unit\.ThermalRun_', 'cli\.thermal\.', 'cli\.thermal\.workflow$', 'architecture\.thermalcli-ownership',
    'unit\.ThermalCli_(SteadySolve|SteadyRobinOnly|FullyConstrainedSteady|SingularSteadyFails|SteadyNeedsOnlyConductivity)$',
    'unit\.ThermalCli_(TransientSolve|TransientPureNeumann|TransientUsesActualStoredTimes|TransientFinalPartialStep)$',
    'unit\.ThermalCli_.*CoreEquivalence', 'unit\.ThermalCli_.*(Stdout|Output)', 'unit\.ThermalCli_.*ExitCode',
    'unit\.StructuralCli', 'unit\.MeshingCli', 'cli\.structural\.', 'cli\.mesh\.', 'unit\.Thermal', 'unit\.Structural', '^architecture\.'
)
$fresh = @{}
foreach ($preset in $presets) {
    $bin = "$root\$preset\bin"
    "== $preset" | Out-File $summary -Append
    foreach ($exe in "bettercad-cli.exe", "bettercad_tests.exe") {
        $f = Get-Item (Join-Path $bin $exe)
        $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
        "  $exe $($f.Length) $h  $($f.FullName)" | Out-File $summary -Append
    }
    foreach ($dll in "libbettercad_thermal.dll", "libbettercad_cli_lib.dll") {
        if (Test-Path (Join-Path $bin $dll)) {
            $f = Get-Item (Join-Path $bin $dll)
            "  $dll $($f.Length) " + (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower() | Out-File $summary -Append
        }
    }
    $n = & ctest --test-dir "$root\$preset" -N 2>&1 | Select-String "Total Tests:"
    "  ctest -N: $n" | Out-File $summary -Append
    foreach ($filter in $filters) {
        $m = & ctest --test-dir "$root\$preset" -N -R $filter 2>&1 | Select-String "Total Tests:"
        "  ctest -N -R $filter : $m" | Out-File $summary -Append
    }
    # THE THERMAL SUITES through the XML reporter, on THIS preset's own test executable.
    foreach ($suite in @(@("ThermalCli_*", "cli"), @("ThermalRun_*", "run"))) {
        $xmlFile = Join-Path $out "$($suite[1])-$preset.xml"
        & (Join-Path $bin "bettercad_tests.exe") $suite[0] -r xml > $xmlFile 2>&1
        $xml = [System.Net.WebUtility]::HtmlDecode((Get-Content $xmlFile -Raw))
        "  $($suite[0]) : " + [regex]::Match($xml, '<OverallResultsCases[^>]*>').Value + "  " + [regex]::Match($xml, '<OverallResults [^>]*>').Value | Out-File $summary -Append
    }
    # THE WORKFLOW'S LEDGER from this preset's own build tree.
    $wf = "$root\$preset\tests\cli-output\thermal-workflow"
    $ledger = Get-Content "$wf\ledger.txt"
    "  workflow ledger lines: $($ledger.Count)" | Out-File $summary -Append
    foreach ($l in ($ledger | Select-Object -First 8)) { "    $l" | Out-File $summary -Append }
    $fresh[$preset] = $ledger
    "  runtime libraries resolved: " + (($ledger | Where-Object { $_ -match '^runtime ' } | Measure-Object).Count) | Out-File $summary -Append
    "  project libraries beside the binary: " + (($ledger | Where-Object { $_ -match '^project-libraries' })) | Out-File $summary -Append
    "  " + ($ledger | Where-Object { $_ -match '^steps ' }) | Out-File $summary -Append
    "  strict re-parse: " + (($ledger | Where-Object { $_ -match 'strict re-parse' }) -join ' ') | Out-File $summary -Append
}

# ---- THE PRESETS AGAINST ONE ANOTHER
"== cross-preset: the workflow's processes (exit code, byte counts, stdout hash) with each preset's build root removed" | Out-File $summary -Append
function Normalise([string]$line, [string]$preset) { return $line.Replace("C:/Users/hasib/bcdev/bc-build/$preset", "<build>") }
$tables = @{}
foreach ($preset in $presets) {
    $rows = @()
    foreach ($l in $fresh[$preset]) { if ($l -match '^\d+ \| ') { $rows += (Normalise $l $preset) } }
    $tables[$preset] = $rows
}
$ref = $tables["debug-ext"]
"  processes: " + (($presets | ForEach-Object { "$_ $($tables[$_].Count)" }) -join "; ") | Out-File $summary -Append
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
"== cross-preset: the payloads that differ are compared as NUMBERS (relative tolerance 1e-12), schema and discrete fields exactly" | Out-File $summary -Append
function Flatten($node, [string]$prefix, $acc) {
    if ($node -is [System.Management.Automation.PSCustomObject]) { foreach ($p in $node.PSObject.Properties) { Flatten $p.Value "$prefix.$($p.Name)" $acc } }
    elseif ($node -is [System.Array]) { for ($i = 0; $i -lt $node.Count; $i++) { Flatten $node[$i] "$prefix[$i]" $acc } }
    else { $acc[$prefix] = $node }
}
foreach ($preset in "release-ext", "debug-shared-ext") {
    $compared = 0; $schemaDiff = 0; $discrete = 0; $numeric = 0; $worst = 0.0; $worstWhere = ""
    foreach ($f in (Get-ChildItem "$root\debug-ext\tests\cli-output\thermal-workflow\payloads" -Filter *.json | Where-Object Length -gt 0)) {
        $other = "$root\$preset\tests\cli-output\thermal-workflow\payloads\$($f.Name)"
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
"== cross-preset: the canonical documents the workflow's edits left behind (SHA-256)" | Out-File $summary -Append
foreach ($doc in "steady.bcad", "transient.bcad", "batch.bcad", "local-size.bcad", "singular.bcad", "no-material.bcad", "unresolved.bcad", "conflict.bcad", "transient-neumann.bcad", "k-only.bcad", "k-only-steady.bcad") {
    $hashes = @()
    foreach ($preset in $presets) { $hashes += (Get-FileHash "$root\$preset\tests\cli-output\thermal-workflow\$doc" -Algorithm SHA256).Hash.ToLower() }
    "  {0,-26} {1}  {2}" -f $doc, $(if (($hashes | Select-Object -Unique).Count -eq 1) { "IDENTICAL in all three presets" } else { "DIFFERS" }), $hashes[0].Substring(0, 16) | Out-File $summary -Append
}
if ($CopyEvidence) {
    foreach ($preset in $presets) {
        $dest = "$q\workflow-$preset"
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        New-Item -ItemType Directory -Force $dest | Out-Null
        Copy-Item "$root\$preset\tests\cli-output\thermal-workflow\ledger.txt" $dest
        Copy-Item "$root\$preset\tests\cli-output\thermal-workflow\payloads" "$dest\payloads" -Recurse
    }
    "evidence copied under $q\workflow-<preset>" | Out-File $summary -Append
}
"postrun-cli finished $(Get-Date -Format o)" | Out-File $summary -Append
Get-Content $summary
