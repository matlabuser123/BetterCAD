# P18-LOAD-001 mutation probes, batch 5: the brief's remaining probes.
#   L43 L44  double-count one boundary facet / one tetrahedron
#   L45      the material enters the prepared-load identity
#   L46      a canonical load gains a persisted mesh-local field
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp = "$root\src\thermal\ThermalLoadVector.cpp"
$summary = "$s\mutations-load5-summary.txt"
"P18-LOAD-001 mutation probes batch 5 started $(Get-Date)" | Out-File $summary

function Probe($name, $file, $find, $replace) {
    & "$s\mutate.ps1" -Name $name -File $file -Find $find -Replace $replace -Filter 'unit\.ThermalLoad_' -Ctest | Out-Null
    $log = Get-Content "$s\mutation-$name.log"
    $verdict = ($log | Select-String -Pattern "VERDICT|VOID|not found").Line -join " "
    $tests = Get-Content "$s\mutation-$name-tests.log" -ErrorAction SilentlyContinue
    $passed = ($tests | Select-String -Pattern "tests passed").Line
    $failed = ($tests | Select-String -Pattern "\(Failed\)" | Measure-Object).Count
    $aborted = ($tests | Select-String -Pattern "\(Subprocess aborted\)|\(SEGFAULT\)|\(Exception|\(Child aborted" | Measure-Object).Count
    "$name : $verdict : $passed : assertion-failed cases $failed : aborted or crashed cases $aborted" | Out-File $summary -Append
}

$hdr = "$root\include\bettercad\thermal\ThermalData.hpp"
Probe "L43-first-boundary-facet-counted-twice" $cpp "    const double share = facetNodalHeat(Area::fromSi(facetArea), flux).si();" "    const double share = facetNodalHeat(Area::fromSi(facetArea), flux).si() * (id == facets.front() ? 2.0 : 1.0);"
Probe "L44-first-tetrahedron-counted-twice" $cpp "        const double share = elementNodalHeat(geometry->volume(), generation).si();" "        const double share = elementNodalHeat(geometry->volume(), generation).si() * (id == elements.front() ? 2.0 : 1.0);"
Probe "L45-material-enters-the-prepared-load-identity" $cpp "    source.loads.assign(loads.begin(), loads.end());" "    source.loads.assign(loads.begin(), loads.end());`n    if (model.conductivity().si() != 45.0) {`n        source.loads.clear();`n    }"
Probe "L46-canonical-surface-load-persists-a-facet-id" $hdr "    HeatFlux flux{};`n`n    friend bool operator==(const SurfaceHeatFluxLoad&, const SurfaceHeatFluxLoad&) = default;" "    HeatFlux flux{};`n    std::uint32_t cachedFacetId = 0;`n`n    friend bool operator==(const SurfaceHeatFluxLoad&, const SurfaceHeatFluxLoad&) = default;"
"P18-LOAD-001 mutation probes batch 5 finished $(Get-Date)" | Out-File $summary -Append
Get-Content $summary