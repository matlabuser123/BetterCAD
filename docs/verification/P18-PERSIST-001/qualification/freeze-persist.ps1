# P18-PERSIST-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, the persistence contract
# fingerprint, the schema fingerprint, the canonical-persistence-boundary fingerprint, the serialization-policy fingerprint, the
# backward-compatibility-policy fingerprint and the ownership fingerprint.
param([string]$Out = "freeze-persist.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")   # .NET file calls do not follow Set-Location
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-persist"
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-PERSIST-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
"HEAD      $((& $git rev-parse HEAD).Trim())" | Out-File $o -Append
"HEAD tree $((& $git rev-parse 'HEAD^{tree}').Trim())" | Out-File $o -Append
"scratch tree $tree" | Out-File $o -Append
foreach ($p in $paths) {
    $h = (& $git rev-parse "${tree}:$p").Trim()
    "{0,-18} {1}" -f $p, $h | Out-File $o -Append
}

function BodyOf([string]$text, [string]$signature) {
    $a = $text.IndexOf($signature)
    if ($a -lt 0) { throw "signature not found: $signature" }
    $b = $text.IndexOf("`n}`n", $a)
    return $text.Substring($a, $b - $a + 2)
}
$tj = [IO.File]::ReadAllText("src/io/json/ThermalAnalysisJson.cpp")
$oj = [IO.File]::ReadAllText("src/io/json/ObjectJson.hpp")
$dj = [IO.File]::ReadAllText("src/io/json/DocumentJson.cpp")

# The persistence contract: the shared private header (the two declarations), the reader/writer implementation, the dispatch, and the
# canonical analysis header whose definition they carry.
$names = @("src/io/json/ObjectJson.hpp", "src/io/json/ThermalAnalysisJson.cpp", "src/io/json/DocumentJson.cpp", "include/bettercad/thermal/ThermalAnalysisObject.hpp", "include/bettercad/thermal/ThermalData.hpp")
$lines = @()
foreach ($n in $names) { $lines += ("{0,-58} {1}" -f $n, (& $git hash-object $n).Trim()) }
[IO.File]::WriteAllText("$s\contract-persist.txt", (($lines) -join "`n") + "`n")
foreach ($l in $lines) { $l | Out-File $o -Append }
"P18_PERSISTENCE_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-persist.txt").Trim())" | Out-File $o -Append

# The schema: every allowed-key list, every enumeration-to-key table, and the key names the writer inserts, in file order.
$schema = @()
foreach ($m in [regex]::Matches($tj, 'requireObject\([^;]*?\{([^}]*)\}')) { $schema += "keys: " + ($m.Groups[1].Value -replace '\s+', ' ').Trim() }
foreach ($m in [regex]::Matches($tj, '\{thermal::[A-Za-z:]+, "[a-z_]+"\}')) { $schema += "enum: " + $m.Value }
foreach ($m in [regex]::Matches($tj, 'json\["([a-z_]+)"\] =')) { $schema += "writes: " + $m.Groups[1].Value }
[IO.File]::WriteAllText("$s\schema-persist.txt", (($schema) -join "`n") + "`n")
"schema: $($schema.Count) lines (allowed keys, enumeration keys, written keys)" | Out-File $o -Append
foreach ($l in $schema) { "  schema $l" | Out-File $o -Append }
"P18_SCHEMA_FINGERPRINT $((& $git hash-object "$s\schema-persist.txt").Trim())" | Out-File $o -Append

# The canonical persistence boundary: what the definition holds (its member list) and what the gate forbids the file to name.
$def = [regex]::Match([IO.File]::ReadAllText("include/bettercad/thermal/ThermalAnalysisObject.hpp"), 'struct ThermalAnalysisDefinition \{.*?\n\};', 'Singleline').Value
$gate = [IO.File]::ReadAllText("tests/architecture/CheckThermalPersistenceOwnership.cmake")
$sets = @()
foreach ($name in '_forbidden_includes', '_derived', '_mesh_identity', '_material', '_resolution', '_units', '_text_parse', '_nondeterminism') {
    $sets += [regex]::Match($gate, "set\($name `"[^`"]*`"\)").Value
}
[IO.File]::WriteAllText("$s\boundary-persist.txt", $def + "`n" + (($sets) -join "`n") + "`n")
"canonical boundary: the definition (bytes $($def.Length)) and $($sets.Count) forbidden-name sets" | Out-File $o -Append
"P18_CANONICAL_PERSISTENCE_BOUNDARY_FINGERPRINT $((& $git hash-object "$s\boundary-persist.txt").Trim())" | Out-File $o -Append

# The serialization policy: the writer, the format-wide dump function and the format constants, and the golden constants the test pins.
$writer = BodyOf $tj 'Result<Json> thermalAnalysisToJson(const thermal::ThermalAnalysis& analysis) {'
$dump = BodyOf ([IO.File]::ReadAllText("src/io/json/JsonReader.cpp")) 'Result<std::string> dumpJson(const Json& value) {'
$golden = ([regex]::Matches([IO.File]::ReadAllText("tests/io/ThermalAnalysisFileTests.cpp"), 'CHECK\(fnv\([a-zA-Z]+\) == 0x[0-9a-f]+ULL\);') | ForEach-Object { $_.Value }) -join "`n"
$versions = ([regex]::Matches([IO.File]::ReadAllText("include/bettercad/io/DocumentFile.hpp"), 'inline constexpr int k[A-Za-z]+ = \d+;') | ForEach-Object { $_.Value }) -join "`n"
[IO.File]::WriteAllText("$s\policy-persist.txt", $writer + $dump + $golden + "`n" + $versions + "`n")
"serialization policy: writer + dumpJson + golden constants + format constants (bytes $((Get-Item "$s\policy-persist.txt").Length))" | Out-File $o -Append
$golden -split "`n" | ForEach-Object { "  golden $_" } | Out-File $o -Append
"P18_SERIALIZATION_POLICY_FINGERPRINT $((& $git hash-object "$s\policy-persist.txt").Trim())" | Out-File $o -Append

# The backward-compatibility policy: the format constants, the dispatch branches, and the blobs of the real pre-P18 documents the tests load.
$compat = @($versions)
foreach ($f in 'examples/models/meshed_plate.bcad', 'examples/models/analysed_plate.bcad', 'examples/models/plate.bcad') { $compat += "$f $((& $git hash-object $f).Trim())" }
$compat += ([regex]::Matches($dj, 'if \(\*type == "thermal-analysis"\) \{.*?\n    \}', 'Singleline') | ForEach-Object { $_.Value })
[IO.File]::WriteAllText("$s\compat-persist.txt", (($compat) -join "`n") + "`n")
"backward compatibility: format constants, three pre-P18 document blobs and the read dispatch" | Out-File $o -Append
foreach ($c in $compat[1..3]) { "  $c" | Out-File $o -Append }
"P18_BACKWARD_COMPATIBILITY_POLICY_FINGERPRINT $((& $git hash-object "$s\compat-persist.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckThermalPersistenceOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'thermalpersist-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-persist.txt", (($own) -join "`n") + "`n")
"ownership: $($own.Count) files (the checker and its fixtures)" | Out-File $o -Append
"THERMAL_PERSIST_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-persist.txt").Trim())" | Out-File $o -Append

$d1 = (& $git hash-object src/io/CMakeLists.txt).Trim()
$d2 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d3 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-persist.txt", "src/io/CMakeLists.txt     $d1`nCheckLayering.cmake       $d2`nCheckTargetDependencies.cmake $d3`n")
"src/io/CMakeLists.txt         $d1" | Out-File $o -Append
"CheckLayering.cmake           $d2" | Out-File $o -Append
"CheckTargetDependencies.cmake $d3" | Out-File $o -Append
"ADR_056_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-persist.txt").Trim())" | Out-File $o -Append

# Everything under src, include and apps that this milestone must NOT have touched (byte-identical to HEAD), apart from the four files it
# is meant to change: the shared private header, the dispatch, the io CMake list and the new reader/writer.
$expected = 'src/io/json/ObjectJson.hpp', 'src/io/json/DocumentJson.cpp', 'src/io/CMakeLists.txt', 'src/io/json/ThermalAnalysisJson.cpp'
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
foreach ($b in $different) { "  DIFFERENT $b $(if ($expected -contains $b) { '(expected)' } else { '(UNEXPECTED)' })" | Out-File $o -Append }
foreach ($b in $new) { "  NEW       $b $(if ($expected -contains $b) { '(expected)' } else { '(UNEXPECTED)' })" | Out-File $o -Append }
foreach ($b in 'src/io/json/ThermalAnalysisJson.cpp', 'tests/io/ThermalAnalysisFileTests.cpp', 'examples/models/thermal_plate.bcad') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
