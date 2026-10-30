# P19-MESH-001 mutation campaign. Each probe replaces ONE exact text (which must occur exactly ONCE) in ONE file, rebuilds bettercad_tests
# (debug-ext), runs the CFD cases through CTest (CfdMesh, CfdInput, CfdData, CfdFluid), records the verdict and ALWAYS restores the file byte
# for byte. KILLED = some case failed; SURVIVED = all passed; VOID = the mutant did not compile; NOT FOUND / NOT UNIQUE = a script defect,
# not a verdict.
# ASCII ONLY (PowerShell 5.1 reads a .ps1 as ANSI).
param([string]$Only = "")
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\mutation-mesh"
New-Item -ItemType Directory -Force $s | Out-Null
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$pk = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$env:Path = "$pk\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$tree = "C:\Users\hasib\bcdev\bc-build\debug-ext"
$gate = "$root\src\cfd\CfdMesh.cpp"
$header = "$root\include\bettercad\cfd\CfdMesh.hpp"
$probes = @(
    @("MP01", $gate, "a zero-volume cell is accepted", "v.si() <= 0.0) {", "v.si() < 0.0) {"),
    @("MP02", $gate, "a non-finite volume is accepted", "if (!std::isfinite(v.si()) || v.si() <= 0.0) {", "if (v.si() <= 0.0) {"),
    @("MP03", $gate, "the opposite vertex is taken as 3 - k (the first draft's bug)", "record.apex = tet.nodes[kTetFaceOpposite[k]];", "record.apex = tet.nodes[3 - k];"),
    @("MP04", $header, "the opposite-vertex table swaps its last two entries", "kTetFaceOpposite{3, 2, 0, 1};", "kTetFaceOpposite{3, 2, 1, 0};"),
    @("MP05", $gate, "the orientation of a shared face is not checked", "if (record.claims == 2 && !reversed(record.wound, wound)) {", "if (record.claims == 2 && false && !reversed(record.wound, wound)) {"),
    @("MP06", $gate, "reversed() compares with the owner's winding, not its reverse", "const std::array<NodeId, 3> back{wound[0], wound[2], wound[1]};", "const std::array<NodeId, 3> back{wound[0], wound[1], wound[2]};"),
    @("MP07", $gate, "reversed() tries no rotation but the first", "for (std::size_t shift = 0; shift < 3; ++shift) {", "for (std::size_t shift = 0; shift < 1; ++shift) {"),
    @("MP08", $gate, "a shared face does not join its cells", "                sets.join(record.ownerSlot, cell);", ""),
    @("MP09", $gate, "a union keeps the HIGHER root", "        if (ra < rb) {`n            parent_[rb] = ra;`n        } else if (rb < ra) {`n            parent_[ra] = rb;`n        }", "        if (ra < rb) {`n            parent_[ra] = rb;`n        } else if (rb < ra) {`n            parent_[rb] = ra;`n        }"),
    @("MP10", $gate, "the stray is any cell that is not its own root", "if (root != 0 && !stray) {", "if (root != cell && !stray) {"),
    @("MP11", $gate, "two pieces are accepted", "if (inspection.census.components > 1) {", "if (inspection.census.components > 2) {"),
    @("MP12", $gate, "the disconnected count saturates at one", "                         inspection.census.components - 1);", "                         1);"),
    @("MP13", $gate, "a three-way face is counted as interior", "} else if (record.claims == 2) {", "} else if (record.claims >= 2) {"),
    @("MP14", $gate, "only a two-way face refuses a triangle laid on it", "if (face->second.claims != 1) {", "if (face->second.claims == 2) {"),
    @("MP15", $gate, "the shared-face ownership refusal falls through", "                                      face->second.claims));`n            continue;", "                                      face->second.claims));"),
    @("MP16", $gate, "a duplicate triangle is not refused", "triangle.id); !inserted) {", "triangle.id); false) {"),
    @("MP17", $gate, "the outward test is reversed", "if (toward > 0.0) {", "if (toward < 0.0) {"),
    @("MP18", $gate, "a degenerate triangle passes", "} else if (!(toward < 0.0)) {", "} else if (false) {"),
    @("MP19", $gate, "an uncovered face is accepted", "if (record.claims == 1 && !covered.contains(key)) {", "if (false) {"),
    @("MP20", $gate, "an incomplete attribution is accepted", "if (!mapping.complete()) {", "if (false) {"),
    @("MP21", $gate, "a report counting other facets is accepted", "} else if (mapping.boundaryFacetCount != mesh.triangles().size()) {", "} else if (false) {"),
    @("MP22", $gate, "the attribution count saturates at one", "std::max<std::size_t>(defects, 1));", "std::min<std::size_t>(defects, 1));"),
    @("MP23", $gate, "two findings are reported out of order", "        {CfdMeshProblem::DisconnectedCells, &disconnected},`n        {CfdMeshProblem::InvalidFaceOwnership, &ownership},", "        {CfdMeshProblem::InvalidFaceOwnership, &ownership},`n        {CfdMeshProblem::DisconnectedCells, &disconnected},"),
    @("MP24", $gate, "the refusal joins its findings with a comma", "joined += ""; "";", "joined += "", "";"),
    @("MP25", $gate, "the refusal has the wrong error code", "return Error{ErrorCode::FailedPrecondition,", "return Error{ErrorCode::InvalidArgument,"),
    @("MP26", $header, "firstProblem reports the last finding", "return findings.front().problem;", "return findings.back().problem;"),
    @("MP27", $gate, "the contract records no geometry revision", "CfdMeshContract(mesh.stamp(), model.geometryRevision(),", "CfdMeshContract(mesh.stamp(), meshing::GeometryRevision{},"),
    @("MP28", $gate, "the contract counts no unnamed patch", "map.report().unnamedFaceCount);", "0);"),
    @("MP29", $gate, "the contract counts only the named patches", "map.faces().size(),", "map.report().namedFaceCount,"),
    @("MP30", $gate, "a tally keeps only its last count", "count += occurrences;", "count = occurrences;"),
    @("MP31", $gate, "an empty mesh is not refused as empty", "if (mesh.tetrahedra().empty()) {", "if (false) {")
)
$progress = "$s\progress.txt"
foreach ($p in $probes) {
    $name, $file, $what, $find, $replace = $p
    if ($Only -and $name -ne $Only) { continue }
    # The probe texts are double-quoted, so their `n is already a line break (the sources use LF).
    $original = [IO.File]::ReadAllText($file)
    $occurrences = ([regex]::Matches($original, [regex]::Escape($find))).Count
    if ($occurrences -eq 0) { "$name | $what | NOT FOUND" | Add-Content $progress; continue }
    if ($occurrences -ne 1) { "$name | $what | NOT UNIQUE ($occurrences)" | Add-Content $progress; continue }
    [IO.File]::WriteAllText($file, $original.Replace($find, $replace), (New-Object Text.UTF8Encoding $false))
    $verdict = "UNKNOWN"; $summary = ""; $failed = ""
    try {
        $buildOut = & cmake --build $tree --target bettercad_tests 2>&1
        $buildOut | Out-File "$s\$name-build.log" -Width 400
        if ($LASTEXITCODE -ne 0) {
            $verdict = "VOID (did not compile)"
        } else {
            $ctestOut = & ctest --test-dir $tree -R "^unit\.(CfdMesh_|CfdInput_|CfdData_|CfdFluid_)" -j 6 --timeout 900 2>&1
            $ctestOut | Out-File "$s\$name-tests.log" -Width 400
            $summary = ($ctestOut | Select-String -Pattern "tests passed").Line -join " "
            $failed = ($ctestOut | Select-String -Pattern "^\s*\d+ - (\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join "; "
            $verdict = if (($ctestOut | Out-String) -match "100% tests passed") { "SURVIVED" } else { "KILLED" }
        }
    } finally {
        [IO.File]::WriteAllText($file, $original, (New-Object Text.UTF8Encoding $false))
    }
    $restored = if ([IO.File]::ReadAllText($file) -ceq $original) { "restored" } else { "NOT RESTORED" }
    "$name | $what | $verdict | $summary | $failed | $restored" | Add-Content $progress
}
"campaign finished $(Get-Date -Format o)" | Add-Content $progress
