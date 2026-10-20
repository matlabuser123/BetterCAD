$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSolve.cpp"
$LS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\linalg\SymmetricSolve.cpp"
$WS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\structural\StructuralSolve.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[thermal][solve],[linalg]"
M 'S01b' 'beff = Qf (the Kfc Tc term omitted)' $TS $F 'out.beff_[equation] = out.qf_[equation] - coupled;' 'out.beff_[equation] = out.qf_[equation] - 0.0 * coupled;'
M 'S02b' 'prescribed temperatures forced to zero' $TS $F 'out.tc_.push_back(constraint.temperature.si());' 'out.tc_.push_back(0.0 * constraint.temperature.si());'
M 'S15b' 'prescribed conditions dropped from the solution source' $TS $F '.temperatures = std::move(temperatureConditions),' '.temperatures = std::vector<ThermalBoundaryCondition>(temperatureConditions.size() > 1000000 ? temperatureConditions : std::vector<ThermalBoundaryCondition>{}),'
M 'S19b' 'rounding noise counts as an anchor (tolerance zero)' $TS $F 'anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;' 'anchoredRow[row] = std::abs(sum) > 0.0 * absolute;'
M 'S30' 'non-finite reduced right-hand side accepted' $TS $F 'if (!std::isfinite(out.beff_[equation])) {' 'if (out.beff_.size() > 0xFFFFFFFFFFFFULL) {'
M 'L04' 'residual finiteness check removed' $LS $F 'if (!std::isfinite(entry)) {' 'if (rows > 0xFFFFFFFFFFFFULL) {'"" | Out-File "$scr\mutation-solve2-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-solve2-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-solve2-progress.txt" -Append
