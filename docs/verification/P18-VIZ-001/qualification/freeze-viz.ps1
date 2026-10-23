# P18-VIZ-001 freeze: the eight-path fingerprint from a scratch index, exactly as qualify.cmd computes it, the visualization contract
# fingerprint, the field-mapping fingerprint, the unit-boundary fingerprint, the classifier-policy fingerprint, the ownership fingerprint
# and the dependency fingerprint.
param([string]$Out = "freeze-viz.txt")
$git = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd\git.exe"
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
[IO.Directory]::SetCurrentDirectory("C:\Users\hasib\OneDrive\Desktop\CAD")   # .NET file calls do not follow Set-Location
$paths = "apps", "include", "src", "tests", "examples", "cmake", "CMakeLists.txt", "CMakePresets.json"
$env:GIT_INDEX_FILE = "$s\scratch-index-viz"
if (Test-Path $env:GIT_INDEX_FILE) { Remove-Item -LiteralPath $env:GIT_INDEX_FILE }
& $git read-tree HEAD 2>$null
& $git add -A -- $paths 2>$null
$tree = (& $git write-tree).Trim()
$env:GIT_INDEX_FILE = $null
$o = "$s\$Out"
"P18-VIZ-001 freeze $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $o
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
$view = [IO.File]::ReadAllText("src/renderer/ThermalResultView.cpp")
$overlay = [IO.File]::ReadAllText("src/renderer/ThermalOverlay.cpp")

# The visualization contract: the four public headers a later milestone (the thermal commands, the GUI panels) relies on, with the
# Viewer header that carries the one additive overload.
$names = "ThermalResultView.hpp", "ThermalContour.hpp", "ThermalInspection.hpp", "ThermalOverlay.hpp", "Viewer.hpp"
$lines = @()
foreach ($n in $names) { $lines += ("{0,-24} {1}" -f $n, (& $git hash-object "include/bettercad/renderer/$n").Trim()) }
[IO.File]::WriteAllText("$s\contract-viz.txt", (($lines) -join "`n") + "`n")
foreach ($l in $lines) { $l | Out-File $o -Append }
"P18_VIZ_CONTRACT_FINGERPRINT $((& $git hash-object "$s\contract-viz.txt").Trim())" | Out-File $o -Append

# The field mapping: which stored member each channel reads, its unit and its location (descriptorOf), how a scalar is taken from a stored
# value (channelValue), and the classifier that walks a stored state.
$desc = BodyOf $view 'ThermalChannelDescriptor descriptorOf(ThermalChannel channel) noexcept {'
$norm = BodyOf $view 'double gradientDisplayNorm(const thermal::TemperatureGradient3& gradient) noexcept {'
$cv1 = BodyOf $view 'std::optional<double> channelValue(ThermalChannel channel, Temperature temperature) noexcept {'
$cv2 = BodyOf $view 'std::optional<double> channelValue(ThermalChannel channel, const thermal::TemperatureGradient3& gradient,'
$cls = BodyOf $view 'Result<ThermalScalarField> classifyThermalState(ThermalChannel channel, const thermal::ThermalPostState& stored,'
[IO.File]::WriteAllText("$s\mapping-viz.txt", $desc + $norm + $cv1 + $cv2 + $cls)
"field mapping: descriptorOf, gradientDisplayNorm, channelValue x2, classifyThermalState (bytes $((Get-Item "$s\mapping-viz.txt").Length))" | Out-File $o -Append
"P18_VIZ_FIELD_MAPPING_FINGERPRINT $((& $git hash-object "$s\mapping-viz.txt").Trim())" | Out-File $o -Append

# The unit boundary: the presentation adapter that holds the one metre-to-millimetre conversion, the overlay presentation that passes through it,
# the thermal view's own unit statements, and the offscreen test that measures the boundary against a CAD solid.
$ub = @()
foreach ($f in 'src/renderer/occt/OcctOverlayPresentation.cpp', 'src/renderer/occt/OcctOverlayPresentation.hpp', 'src/renderer/occt/OcctMeshPresentation.cpp', 'tests/renderer/ThermalVizViewerTests.cpp') {
    $ub += "$f $((& $git hash-object $f).Trim())"
}
$ub += "descriptorOf $((& $git hash-object "$s\mapping-viz.txt").Trim())"
$conv = ([regex]::Matches([IO.File]::ReadAllText("src/renderer/occt/OcctOverlayPresentation.cpp"), 'toModelLength')).Count
$ub += "toModelLength occurrences in OcctOverlayPresentation.cpp: $conv"
[IO.File]::WriteAllText("$s\unit-boundary-viz.txt", (($ub) -join "`n") + "`n")
foreach ($l in $ub) { "unit boundary $l" | Out-File $o -Append }
"P18_VIZ_UNIT_BOUNDARY_FINGERPRINT $((& $git hash-object "$s\unit-boundary-viz.txt").Trim())" | Out-File $o -Append

# The classifier policy: the cardinality rule, its status enumeration, and the glyph builders' use of it.
$card = BodyOf $overlay 'BoundaryCardinality classifyBoundaryCardinality(BoundaryTargetKind kind, std::uint64_t id,'
$statusEnum = [regex]::Match([IO.File]::ReadAllText("include/bettercad/renderer/ThermalOverlay.hpp"), 'enum class BoundaryClassStatus : std::uint8_t \{[^}]*\};').Value
[IO.File]::WriteAllText("$s\classifier-viz.txt", $card + $statusEnum + "`n")
"classifier policy: classifyBoundaryCardinality, BoundaryClassStatus (bytes $((Get-Item "$s\classifier-viz.txt").Length))" | Out-File $o -Append
"P18_VIZ_CLASSIFIER_POLICY_FINGERPRINT $((& $git hash-object "$s\classifier-viz.txt").Trim())" | Out-File $o -Append

# Ownership: the checker and its fixtures.
$own = @()
foreach ($f in 'tests/architecture/CheckThermalVizOwnership.cmake') { $own += "$f $((& $git hash-object $f).Trim())" }
foreach ($f in (Get-ChildItem -Recurse -File tests/architecture/fixtures | Where-Object { $_.FullName -match 'thermalviz-' } | Sort-Object FullName)) {
    $rel = $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
    $own += "$rel $((& $git hash-object $rel).Trim())"
}
[IO.File]::WriteAllText("$s\ownership-viz.txt", (($own) -join "`n") + "`n")
foreach ($l in $own) { "ownership   $l" | Out-File $o -Append }
"THERMAL_VIZ_OWNERSHIP_FINGERPRINT $((& $git hash-object "$s\ownership-viz.txt").Trim())" | Out-File $o -Append

$d1 = (& $git hash-object src/renderer/CMakeLists.txt).Trim()
$d2 = (& $git hash-object src/thermal/CMakeLists.txt).Trim()
$d3 = (& $git hash-object tests/architecture/CheckLayering.cmake).Trim()
$d4 = (& $git hash-object tests/architecture/CheckTargetDependencies.cmake).Trim()
[IO.File]::WriteAllText("$s\dependency-viz.txt", "src/renderer/CMakeLists.txt    $d1`nsrc/thermal/CMakeLists.txt     $d2`nCheckLayering.cmake           $d3`nCheckTargetDependencies.cmake $d4`n")
"src/renderer/CMakeLists.txt    $d1" | Out-File $o -Append
"src/thermal/CMakeLists.txt     $d2" | Out-File $o -Append
"CheckLayering.cmake           $d3" | Out-File $o -Append
"CheckTargetDependencies.cmake $d4" | Out-File $o -Append
"ADR_050_DEPENDENCY_FINGERPRINT $((& $git hash-object "$s\dependency-viz.txt").Trim())" | Out-File $o -Append

# The qualified predecessors that this milestone must NOT have touched (byte-identical to HEAD): the whole thermal module, the meshing module,
# the linalg and structural solve files, the structural visualization (P17), and the OCCT mesh presentation that holds the unit boundary.
$untouched = @()
foreach ($d in 'src/thermal', 'include/bettercad/thermal', 'src/meshing', 'include/bettercad/meshing', 'src/structural', 'include/bettercad/structural', 'src/linalg', 'include/bettercad/linalg', 'src/core', 'include/bettercad/core') {
    foreach ($f in (Get-ChildItem -File -Recurse $d | Sort-Object FullName)) { $untouched += $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/') }
}
foreach ($f in (Get-ChildItem -File src/renderer, include/bettercad/renderer | Where-Object { $_.Name -match '^Structural|^MeshView|^MeshHighlight|^GeometryMeshMap|^MeshContour' } | Sort-Object FullName)) {
    $untouched += $f.FullName.Substring((Get-Location).Path.Length + 1).Replace('\', '/')
}
$untouched += 'src/renderer/occt/OcctMeshPresentation.cpp', 'src/renderer/occt/OcctMeshPresentation.hpp'
$equal = 0
$different = 0
$list = @()
foreach ($b in $untouched) {
    $work = (& $git hash-object $b).Trim()
    $head = (& $git rev-parse "HEAD:$b" 2>$null)
    if ($head) { $head = $head.Trim() } else { $head = "(absent)" }
    if ($work -eq $head) { $equal++ } else { $different++; $list += $b }
}
"untouched files: $($untouched.Count) equal $equal different $different" | Out-File $o -Append
foreach ($b in $list) { "  DIFFERENT $b" | Out-File $o -Append }
foreach ($b in 'src/renderer/ThermalResultView.cpp', 'src/renderer/ThermalContour.cpp', 'src/renderer/ThermalInspection.cpp', 'src/renderer/ThermalOverlay.cpp', 'tests/renderer/ThermalVizFieldTests.cpp', 'tests/renderer/ThermalVizContourTests.cpp', 'tests/renderer/ThermalVizStateTests.cpp', 'tests/renderer/ThermalVizBoundaryTests.cpp', 'tests/renderer/ThermalVizViewerTests.cpp', 'tests/renderer/ThermalVizTestSupport.hpp', 'tests/compile_fail/ThermalVizMisuse.cpp') {
    "blob {0,-52} {1}" -f $b, (& $git hash-object $b).Trim() | Out-File $o -Append
}
"status:" | Out-File $o -Append
& $git status --short | Out-File $o -Append
Get-Content $o
