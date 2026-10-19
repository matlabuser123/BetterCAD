$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$SYS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSystem.cpp"
$SPR = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSparse.cpp"
$m = @()
function M($n,$desc,$f,$find,$rep){ $script:m += ,@($n,$desc,$f,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
M 'B01' 'Ke added to C' $SYS 'capacityValues[*slot] += (*ce)(a, b).si();' 'capacityValues[*slot] += (*ke)(a, b).si();'
M 'B02' 'Ce added to K as well' $SYS 'capacityValues[*slot] += (*ce)(a, b).si();' 'capacityValues[*slot] += (*ce)(a, b).si(); if (const auto s2 = slotOf(conductanceLayout, row, column)) { conductanceValues[*s2] += (*ce)(a, b).si(); }'
M 'B03' 'last element omitted' $SYS 'const Result<Tet4Corners> corners = cornersOf(mesh, tet);' "if (&tet == &tets.back()) { continue; }`n        const Result<Tet4Corners> corners = cornersOf(mesh, tet);"
M 'B04' 'first element counted twice in K' $SYS 'conductanceValues[*slot] += (*ke)(a, b).si();' 'conductanceValues[*slot] += (&tet == &tets.front() ? 2.0 : 1.0) * (*ke)(a, b).si();'
M 'B05' 'combined and per-load heat input both added' $SYS '.heatInput = preparedLoads.heatInput(),' '.heatInput = [&preparedLoads]() { static thread_local std::vector<Power> v; v.assign(preparedLoads.heatInput().begin(), preparedLoads.heatInput().end()); for (Power& p : v) { p = Power::fromSi(2.0 * p.si()); } return std::span<const Power>(v); }(),'
M 'B06' 'penalty-enforced Dirichlet on unknown 0' $SYS '// Validate before publishing. The symmetry gate is a refusal and never a repair.' "conductanceValues[0] += 1.0e12;`n    heat[0] += 1.0e12 * 350.0;"
M 'B07' 'entries below a threshold dropped' $SYS 'conductanceValues[*slot] += (*ke)(a, b).si();' 'conductanceValues[*slot] += (std::abs((*ke)(a, b).si()) < 0.6 ? 0.0 : (*ke)(a, b).si());'
M 'B08' 'transient with a missing capacity silently becomes steady' $SYS 'resolveThermalMaterial(document, model.body(), mode)' 'resolveThermalMaterial(document, model.body(), ThermalAnalysisMode::Steady)'
M 'B09' 'C / dt added into K' $SYS 'capacityValues[*slot] += (*ce)(a, b).si();' 'capacityValues[*slot] += (*ce)(a, b).si(); if (const auto s2 = slotOf(conductanceLayout, row, column)) { conductanceValues[*s2] += (*ce)(a, b).si() / 0.1; }'
M 'B10' 'convection matrix recomputed as a half off-diagonal' $SYS 'conductanceValues[*slot] += facet.term(a, b).si();' 'conductanceValues[*slot] += facet.term(a, b).si() * (a == b ? 1.0 : 0.5);'
M 'B11' 'matrix size taken from the largest handle' $SYS 'const std::size_t rows = static_cast<std::size_t>(map.dofCount());' 'const std::size_t rows = std::max<std::size_t>(static_cast<std::size_t>(map.dofCount()), std::min<std::size_t>(static_cast<std::size_t>(mesh.nodes().back().id.value()) + 1, 100000));'
M 'B12' 'convection matrix counted twice' $SYS 'conductanceValues[*slot] += facet.term(a, b).si();' 'conductanceValues[*slot] += 2.0 * facet.term(a, b).si();'
M 'B13' 'convection source counted twice' $SYS 'heat[row] += facet.term.source(a).si();' 'heat[row] += 2.0 * facet.term.source(a).si();'
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$s\mutation-$name.log") { $prev = Get-Content "$s\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$s\mutate.ps1" -Name $name -File $e[2] -Find $e[3] -Replace $e[4] -Filter "[thermal][assembly]" | Out-Null
    $v = (Get-Content "$s\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$s\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$s\mutation-driver2-progress.txt" -Append
}
"DONE" | Out-File "$s\mutation-driver2-progress.txt" -Append
