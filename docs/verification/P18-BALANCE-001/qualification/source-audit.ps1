# Source audit for P18-BALANCE-001: what the accounting's two files must not contain, and what they must.
# Usage: powershell -File source-audit.ps1 > source-audit.txt
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = @("include\bettercad\thermal\ThermalBalance.hpp", "src\thermal\ThermalBalance.cpp")
function Strip-Comments([string[]]$lines) {
    # code only: drop // comments (the files use no block comments in code)
    $lines | ForEach-Object { $i = $_.IndexOf('//'); if ($i -ge 0) { $_.Substring(0, $i) } else { $_ } }
}
$patterns = [ordered]@{
    'unordered_map / unordered_set / std::hash'      = 'unordered_|std::hash'
    'std::thread / async / jthread / mutex / atomic' = 'std::thread|std::async|std::jthread|std::mutex|std::atomic'
    'OpenMP / parallel algorithms'                   = 'pragma omp|std::execution'
    'Eigen / a dense matrix'                         = 'Eigen|toDense|MatrixXd|std::vector<std::vector<double>>'
    'a Jacobian / an element or facet integral'      = 'Jacobian|jacobian|computeTet4|integrateConvection|integrateSurfaceHeatFlux|integrateVolumetricHeatSource|computeTri3Convection|facetNodalHeat|elementNodalHeat'
    'a call that solves or steps in time'            = 'solveSteady|solveTransient|solveThermal|effectiveMatrix|reduceThermalSystem|TimeGrid::create'
    'regularisation / epsilon / penalty / pinning'   = 'regulari|penalty|\bpin\b|numeric_limits<double>::epsilon'
    'lumping / row sums / diagonal capacity'         = 'lump|rowSum|row sum|diag'
    'a "remainder" or leftover reaction'             = 'remainder|leftover|closes the|makes the balance'
    'a normal, an outward direction, q . n'          = '\bnormal\b|outward|dotNormal'
    'a Celsius / Fahrenheit conversion'              = '273\.15|Celsius|celsius|Fahrenheit'
    'a unit conversion factor (mm, 1e-3, 1000)'      = '1e-3|1000\.0|1e3\b|millimet'
    'a gradient or a flux field'                     = 'TemperatureGradient|HeatFlux3|gradientFrom'
    'colour / range / legend / clipping'             = 'colou?r|legend|clip|colormap'
    'rand / time( / chrono / clock'                  = '\brand\b|\btime\s*\(|chrono|clock'
    'a persistence or command-history type'          = 'serialize|Serialize|CommandHistory|persist'
    'a time-dependent coefficient'                   = 'loadAt|timeFunction|timeProfile|timeDependent'
    'the NOMINAL step size where the actual one belongs' = 'nominalStep'
}
foreach ($f in $files) {
    $path = Join-Path $root $f
    $all = Get-Content $path
    $code = Strip-Comments $all
    "=== $f  ($($all.Count) lines)"
    foreach ($k in $patterns.Keys) {
        $inCode = @($code | Select-String -Pattern $patterns[$k])
        $inAll = @($all | Select-String -Pattern $patterns[$k])
        "{0,-52} code: {1,3}   anywhere (incl. comments): {2,3}" -f $k, $inCode.Count, $inAll.Count
        foreach ($m in $inCode) { "    CODE   " + $m.Line.Trim() }
    }
}
"=== what the implementation MUST contain (code lines)"
$cpp = Strip-Comments (Get-Content (Join-Path $root "src\thermal\ThermalBalance.cpp"))
foreach ($pair in @(
    @('the convection exchange from the BC terms (residualFrom)',   'residualFrom\('),
    @('the load totals from the contributions (heatRate)',          'heatRate'),
    @('the sparse product K T (multiply)',                          '\.multiply\('),
    @('the actual step size (stepAt)',                              'stepAt\('),
    @('the reaction from the residual (+r at the constraint)',      'fullResidual\[dof\]'),
    @('the one tolerance policy (balanceWithinTolerance)',          'balanceWithinTolerance\('),
    @('the solver''s own currentness (requireCurrent)',             'requireCurrent\('),
    @('the solution''s source by value (postSourceOf)',             'postSourceOf\('),
    @('the sample times are the grid''s (timeAt)',                  'timeAt\('))) {
    $hits = @($cpp | Select-String -Pattern $pair[1])
    "{0,-60} {1,3} line(s)" -f $pair[0], $hits.Count
}
"=== std::abs in code (each must be a MAGNITUDE for a scale or a tolerance, never a heat rate that is reported)"
$n = 0
foreach ($line in (Get-Content (Join-Path $root "src\thermal\ThermalBalance.cpp"))) {
    $n++
    $c = $line; $i = $c.IndexOf('//'); if ($i -ge 0) { $c = $c.Substring(0, $i) }
    if ($c -match 'std::abs') { "{0}:{1}: {2}" -f 'ThermalBalance.cpp', $n, $line.Trim() }
}
"=== comment lines that mention a forbidden thing, each of which must say it is ABSENT"
foreach ($f in $files) {
    $path = Join-Path $root $f
    $n = 0
    foreach ($line in (Get-Content $path)) {
        $n++
        if ($line -match '//' -and $line -match 'remainder|lump|re-integrat|integrate again|leftover|whatever closes|Celsius') {
            "{0}:{1}: {2}" -f (Split-Path $f -Leaf), $n, $line.Trim()
        }
    }
}
