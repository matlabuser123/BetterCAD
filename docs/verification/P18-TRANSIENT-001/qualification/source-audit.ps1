# Source audit for P18-TRANSIENT-001: what the transient solver's two files must not contain.
# Usage: powershell -File source-audit.ps1 > source-audit.txt
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$files = @("include\bettercad\thermal\ThermalTransient.hpp", "src\thermal\ThermalTransient.cpp")
function Strip-Comments([string[]]$lines) {
    # code only: drop // comments (the files use no block comments in code)
    $lines | ForEach-Object { $i = $_.IndexOf('//'); if ($i -ge 0) { $_.Substring(0, $i) } else { $_ } }
}
$patterns = [ordered]@{
    'unordered_map / unordered_set / std::hash' = 'unordered_|std::hash'
    'std::thread / async / jthread / mutex'     = 'std::thread|std::async|std::jthread|std::mutex|std::atomic'
    'OpenMP / parallel algorithms'              = 'pragma omp|std::execution|parallel'
    'Eigen'                                     = 'Eigen'
    'dense matrix / toDense'                    = 'toDense|Dense2|MatrixXd|std::vector<std::vector<double>>'
    'regularisation / epsilon / penalty'        = 'regulari|epsilon|penalty'
    'pinning / fixed datum'                     = '\bpin|datum|reference temperature'
    'lumping'                                   = 'lump|rowSum|row sum'
    'retry / adaptive / halve / halving'        = 'retry|retries|adaptive|halv|\bdouble.*smaller step'
    'a call of analyseAnchoring'                = 'analyseAnchoring\s*\('
    'rand / time( / chrono / clock'             = '\brand\b|\btime\s*\(|chrono|clock'
    'a time-dependent coefficient'              = 'timeFunction|timeProfile|timeDependent'
}
foreach ($f in $files) {
    $path = Join-Path $root $f
    $all = Get-Content $path
    $code = Strip-Comments $all
    "=== $f  ($($all.Count) lines)"
    foreach ($k in $patterns.Keys) {
        $inCode = @($code | Select-String -Pattern $patterns[$k])
        $inAll = @($all | Select-String -Pattern $patterns[$k])
        "{0,-44} code: {1,3}   anywhere (incl. comments): {2,3}" -f $k, $inCode.Count, $inAll.Count
        foreach ($m in $inCode) { "    CODE   " + $m.Line.Trim() }
    }
}
"=== comment lines that mention a forbidden thing, each of which must say it is ABSENT"
foreach ($f in $files) {
    $path = Join-Path $root $f
    $n = 0
    foreach ($line in (Get-Content $path)) {
        $n++
        if ($line -match '//' -and $line -match 'regulari|epsilon|penalty|lump|retry|adaptive|halv|analyseAnchoring|pin') {
            "{0}:{1}: {2}" -f (Split-Path $f -Leaf), $n, $line.Trim()
        }
    }
}
