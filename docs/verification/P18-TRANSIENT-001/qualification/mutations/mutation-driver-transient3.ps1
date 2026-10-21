$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TT = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalTransient.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[transient]"

M 'TA07b' 'the effective matrix is not rebuilt when dt changes (reformulated: keeps the variable used)' $TT $F 'if (!effective.has_value() || effectiveStep != timeStep) {' 'if (!effective.has_value() || (false && effectiveStep != timeStep)) {'
M 'TS03b' 'initial temperature not checked before the system (reformulated: keeps the parameter used)' $TT $F '    if (Result<void> checked = validate(initial); !checked.has_value()) {' '    if (Result<void> checked = ((void)initial, Result<void>{}); !checked.has_value()) {'
"" | Out-File "$scr\mutation-transient3-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate-t.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-transient3-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-transient3-progress.txt" -Append
