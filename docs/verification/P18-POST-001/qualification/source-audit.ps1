# Source audit for P18-POST-001: what the recovery's two files must not contain, and what they must.
# Usage: powershell -File source-audit-post.ps1 > source-audit.txt
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = @("include\bettercad\thermal\ThermalPost.hpp", "src\thermal\ThermalPost.cpp")
function Strip-Comments([string[]]$lines) {
    # code only: drop // comments (the files use no block comments in code)
    $lines | ForEach-Object { $i = $_.IndexOf('//'); if ($i -ge 0) { $_.Substring(0, $i) } else { $_ } }
}
$patterns = [ordered]@{
    'unordered_map / unordered_set / std::hash'  = 'unordered_|std::hash'
    'std::thread / async / jthread / mutex'      = 'std::thread|std::async|std::jthread|std::mutex|std::atomic'
    'OpenMP / parallel algorithms'               = 'pragma omp|std::execution|parallel'
    'Eigen / dense matrix'                       = 'Eigen|toDense|MatrixXd|std::vector<std::vector<double>>'
    'a Jacobian / adjugate / cofactor / shape'   = 'Jacobian|jacobian|adjugate|cofactor|shapeGradient|ShapeGradient'
    'a call of computeTet4Geometry'              = 'computeTet4Geometry\s*\('
    'a call of gradientFrom'                     = 'gradientFrom\s*\('
    'nodal averaging / smoothing / patch recovery' = 'average|smooth|superconverg|least.squares|patch recovery'
    'a Celsius or Fahrenheit conversion'         = '273\.15|Celsius|celsius|Fahrenheit|degC'
    'a unit conversion factor (mm, 1e-3, 1000)'  = '1e-3|1000\.0|1e3\b|millimet'
    'a convection or generation term in q'       = 'convection|Convection|generation|Generation|heatTransferCoefficient'
    'a storage term C dT/dt'                     = 'capacity|Capacity|dTdt|timeDerivative'
    'a boundary normal or q . n'                 = 'normal|Normal|outward'
    'colour / range / legend / clipping'         = 'colou?r|legend|clip|colormap'
    'rand / time( / chrono / clock'              = '\brand\b|\btime\s*\(|chrono|clock'
    'abs / fabs / max / min on a flux value'     = 'std::abs\(.*flux|fabs|std::max\(.*flux'
    'a persistence or command-history type'      = 'serialize|Serialize|CommandHistory|persist'
}
foreach ($f in $files) {
    $path = Join-Path $root $f
    $all = Get-Content $path
    $code = Strip-Comments $all
    "=== $f  ($($all.Count) lines)"
    foreach ($k in $patterns.Keys) {
        $inCode = @($code | Select-String -Pattern $patterns[$k])
        $inAll = @($all | Select-String -Pattern $patterns[$k])
        "{0,-48} code: {1,3}   anywhere (incl. comments): {2,3}" -f $k, $inCode.Count, $inAll.Count
        foreach ($m in $inCode) { "    CODE   " + $m.Line.Trim() }
    }
}
"=== what the implementation MUST contain (code lines)"
$cpp = Strip-Comments (Get-Content (Join-Path $root "src\thermal\ThermalPost.cpp"))
foreach ($pair in @(
    @('P18-ELEM gradient path (gradientFrom)',        'gradientFrom\('),
    @('P18-ELEM geometry (computeTet4Geometry)',      'computeTet4Geometry\('),
    @('the numbering gather (elementDofs)',           'elementDofs\('),
    @('the Fourier line, minus sign and k',           'HeatFlux\{\} - conductivity \* gradient\.'),
    @('the Euclidean magnitude (HeatFlux3::magnitude)', '\.magnitude\(\)'),
    @('the absolute-temperature rule',                'isAbsoluteTemperature\('),
    @('the solver''s own currentness (requireCurrent)', 'requireCurrent\('),
    @('the mesh stamp, not the counts (owns)',        'mesh\.owns\('),
    @('an ordinal binary search (lower_bound)',       'std::lower_bound'))) {
    $hits = @($cpp | Select-String -Pattern $pair[1])
    "{0,-52} {1,3} line(s)" -f $pair[0], $hits.Count
}
"=== comment lines that mention a forbidden thing, each of which must say it is ABSENT"
foreach ($f in $files) {
    $path = Join-Path $root $f
    $n = 0
    foreach ($line in (Get-Content $path)) {
        $n++
        if ($line -match '//' -and $line -match 'smooth|average|Celsius|superconv|convection term|storage term|no nodal|unit conversion') {
            "{0}:{1}: {2}" -f (Split-Path $f -Leaf), $n, $line.Trim()
        }
    }
}
