# P18-CLI-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, and the CLI fingerprints: the contract,
# the command schema, the structured-output shape, the exit policy and the ownership gate with its fixtures.
param([string]$Out = "freeze-cli.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")   # .NET file calls do not follow Set-Location
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-cli"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-CLI-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

$reports = [IO.File]::ReadAllText("apps/bettercad_cli/ThermalReports.cpp")
$edits = [IO.File]::ReadAllText("apps/bettercad_cli/ThermalEdits.cpp")
$output = [IO.File]::ReadAllText("apps/bettercad_cli/ThermalOutput.hpp")
$commands = [IO.File]::ReadAllText("apps/bettercad_cli/Commands.hpp")
$cli = [IO.File]::ReadAllText("apps/bettercad_cli/Cli.cpp")

# THE CONTRACT: the entry (header and source), the three CLI files and the four shared files that register them.
$names = @("include/bettercad/thermal/ThermalAnalysisRun.hpp", "src/thermal/ThermalAnalysisRun.cpp", "apps/bettercad_cli/ThermalReports.cpp",
           "apps/bettercad_cli/ThermalEdits.cpp", "apps/bettercad_cli/ThermalOutput.hpp", "apps/bettercad_cli/Commands.hpp",
           "apps/bettercad_cli/Cli.cpp", "apps/bettercad_cli/Edits.hpp", "apps/bettercad_cli/EditRegistry.cpp")
$lines = @()
foreach ($n in $names) { $lines += ("{0,-58} {1}" -f $n, (& $git hash-object $n).Trim()) }
[IO.File]::WriteAllText("$s\contract-cli.txt", (($lines) -join "`n") + "`n")
foreach ($l in $lines) { $l | Out-File $o -Append }
"P18_CLI_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-cli.txt").Trim())" | Out-File $o -Append

# THE COMMAND SCHEMA: every usage string (reports and edits) and the thermal block of the command table, in file order.
$schema = @()
foreach ($m in [regex]::Matches($commands, 'kThermal[A-Za-z]+Usage\s*=\s*"[^"]*"')) { $schema += "usage: " + ($m.Value -replace '\s+', ' ') }
foreach ($m in [regex]::Matches($edits, 'EditCommand\{"(thermal-[a-z-]+)",\s*((?:"[^"]*"\s*)+),')) { $schema += "edit: " + $m.Groups[1].Value + " " + (($m.Groups[2].Value -replace '"\s*"', '') -replace '\s+', ' ') }
foreach ($m in [regex]::Matches($cli, 'Command\{"(thermal-[a-z-]+)", (kThermal[A-Za-z]+Usage)')) { $schema += "command: " + $m.Groups[1].Value + " " + $m.Groups[2].Value }
foreach ($m in [regex]::Matches($edits, 'parseArguments\(\s*args,\s*\{([^;]*?)\}\);')) { $schema += "options: " + ($m.Groups[1].Value -replace '\s+', ' ').Trim() }
[IO.File]::WriteAllText("$s\schema-cli.txt", (($schema) -join "`n") + "`n")
"command schema: $($schema.Count) lines (usages, edit verbs, command-table entries, option sets)" | Out-File $o -Append
foreach ($l in $schema) { "  schema $l" | Out-File $o -Append }
"P18_CLI_COMMAND_SCHEMA_FINGERPRINT $((& $git hash-object "$s\schema-cli.txt").Trim())" | Out-File $o -Append

# THE STRUCTURED-OUTPUT SHAPE: every key a payload sets, in file order, per file, and the whole output header.
$shape = @()
foreach ($pair in @(@("reports", $reports), @("output", $output))) {
    foreach ($m in [regex]::Matches($pair[1], '\.set\("([A-Za-z]+)"')) { $shape += "$($pair[0]): $($m.Groups[1].Value)" }
    foreach ($m in [regex]::Matches($pair[1], 'extremumJson\(fin, "([a-z]+)"')) { $shape += "$($pair[0]) idkey: $($m.Groups[1].Value)" }
}
[IO.File]::WriteAllText("$s\shape-cli.txt", (($shape) -join "`n") + "`n" + $output)
"structured output: $($shape.Count) payload keys, plus the whole ThermalOutput.hpp" | Out-File $o -Append
"P18_CLI_STRUCTURED_OUTPUT_FINGERPRINT $((& $git hash-object "$s\shape-cli.txt").Trim())" | Out-File $o -Append

# THE EXIT POLICY: the three-code enumeration, every diagnostic code the thermal files can emit, the exit statements and the edit-code map.
$exit = @()
$exit += ([regex]::Match([IO.File]::ReadAllText("apps/bettercad_cli/Cli.hpp"), 'enum class ExitCode[^}]*\}').Value -replace '\s+', ' ')
foreach ($pair in @(@("reports", $reports), @("edits", $edits))) {
    foreach ($m in [regex]::Matches($pair[1], '(?:failure|rejected|coded)\([^;]*?"([a-z_]+)"')) { $exit += "$($pair[0]) code: $($m.Groups[1].Value)" }
    foreach ($m in [regex]::Matches($pair[1], 'code = "([a-z_]+)";')) { $exit += "$($pair[0]) mapped: $($m.Groups[1].Value)" }
}
foreach ($m in [regex]::Matches($reports, 'return [^;]*ExitCode::[A-Za-z]+[^;]*;')) { $exit += "reports exit: " + ($m.Value -replace '\s+', ' ') }
$exit += ([regex]::Match([IO.File]::ReadAllText("apps/bettercad_cli/EditSupport.hpp"), 'inline EditFailure fromParse\(const Error& error\) \{.*?\n\}', 'Singleline').Value -replace '\s+', ' ')
[IO.File]::WriteAllText("$s\exit-cli.txt", (($exit) -join "`n") + "`n")
"exit policy: $($exit.Count) lines" | Out-File $o -Append
"P18_CLI_EXIT_POLICY_FINGERPRINT $((& $git hash-object "$s\exit-cli.txt").Trim())" | Out-File $o -Append

# OWNERSHIP: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckThermalCliOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'thermalcli-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-cli.txt", (($own) -join "`n") + "`n")
"ownership: $($own.Count) files (the checker and its fixtures)" | Out-File $o -Append
"THERMAL_CLI_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-cli.txt").Trim())" | Out-File $o -Append

# THE WORKFLOW AND ITS TESTS.
$wf = @()
foreach ($f in 'tests/cli/ThermalWorkflow.cmake', 'tests/cli/ThermalCliTests.cpp', 'tests/cli/MiniJson.hpp', 'tests/thermal/ThermalAnalysisRunTests.cpp',
               'tests/cli/ZeroMatchGuard.cmake', 'examples/models/thermal_bare_plate.bcad', 'examples/models/thermal_plate.bcad') {
    $wf += "$f $((& $git hash-object $f).Trim())"
}
[IO.File]::WriteAllText("$s\workflow-cli.txt", (($wf) -join "`n") + "`n")
foreach ($l in $wf) { "  $l" | Out-File $o -Append }
"P18_CLI_WORKFLOW_FINGERPRINT $((& $git hash-object "$s\workflow-cli.txt").Trim())" | Out-File $o -Append

# DEPENDENCIES: the CLI library's link list and the thermal library's source list.
$dep = "apps/bettercad_cli/CMakeLists.txt $((& $git hash-object apps/bettercad_cli/CMakeLists.txt).Trim())`nsrc/thermal/CMakeLists.txt $((& $git hash-object src/thermal/CMakeLists.txt).Trim())`nCheckLayering.cmake $((& $git hash-object tests/architecture/CheckLayering.cmake).Trim())`nCheckTargetDependencies.cmake $((& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim())`n"
[IO.File]::WriteAllText("$s\dependency-cli.txt", $dep)
$dep -split "`n" | Where-Object { $_ } | ForEach-Object { "  $_" | Out-File $o -Append }
"ADR_057_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-cli.txt").Trim())" | Out-File $o -Append

# Everything under src, include and apps that this milestone must NOT have touched (byte-identical to HEAD), apart from the files it is meant to change.
$expected = @('apps/bettercad_cli/Cli.cpp', 'apps/bettercad_cli/Commands.hpp', 'apps/bettercad_cli/Edits.hpp', 'apps/bettercad_cli/EditRegistry.cpp',
              'apps/bettercad_cli/CMakeLists.txt', 'apps/bettercad_cli/ThermalReports.cpp', 'apps/bettercad_cli/ThermalEdits.cpp',
              'apps/bettercad_cli/ThermalOutput.hpp', 'src/thermal/CMakeLists.txt', 'src/thermal/ThermalAnalysisRun.cpp',
              'include/bettercad/thermal/ThermalAnalysisRun.hpp')
$untouched = @()
foreach ($d in 'src', 'include', 'apps') {
    foreach ($f in (Get-ChildItem -File -Recurse $d | Sort-Object FullName)) { $untouched += $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/') }
}
$equal = 0
$different = @()
$new = @()
foreach ($b in $untouched) {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse --verify --quiet "HEAD:$b" 2>$null)
    if ($head) { $head = $head.Trim() } else { $head = $null }
    if ($null -eq $head) { $new += $b } elseif ($work -eq $head) { $equal++ } else { $different += $b }
}
"src + include + apps files: $($untouched.Count) equal $equal different $($different.Count) new $($new.Count)" | Out-File $o -Append
$unexpected = 0
foreach ($b in $different) { $ok = $expected -contains $b; if (-not $ok) { $unexpected++ }; "  DIFFERENT $b $(if ($ok) { '(expected)' } else { '(UNEXPECTED)' })" | Out-File $o -Append }
foreach ($b in $new) { $ok = $expected -contains $b; if (-not $ok) { $unexpected++ }; "  NEW       $b $(if ($ok) { '(expected)' } else { '(UNEXPECTED)' })" | Out-File $o -Append }
"unexpected changes under src, include, apps: $unexpected" | Out-File $o -Append
foreach ($b in 'tests/thermal/ThermalDataTests.cpp', 'tests/thermal/ThermalCommandTests.cpp') {
    "qualified test file {0,-45} {1}  (HEAD {2})" -f $b, (& $git hash-object $b).Trim(), (& $git rev-parse "HEAD:$b").Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
