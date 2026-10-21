$scr = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$TT = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalTransient.cpp"
$m = @()
function M($n,$desc,$f,$filter,$find,$rep){ $script:m += ,@($n,$desc,$f,$filter,($find -replace "`r`n","`n"),($rep -replace "`r`n","`n")) }
$F = "[transient]"

M 'TA21' 'the steady singularity rule leaks in: the conduction matrix K alone must be anchored' $TT $F '    ThermalTransientAlgebra out{.grid = grid};' '    {
        const std::expected<ReducedThermalSystem, SteadySolveFailure> steadyView = reduceThermalSystem(conductance, heat, equations, constraints);
        if (steadyView.has_value() && steadyView->rows() > 0 && analyseAnchoring(*steadyView).unanchored > 0) {
            return std::unexpected(failure(TransientProblem::SingularSystem, ErrorCode::FailedPrecondition, "no datum"));
        }
    }
    ThermalTransientAlgebra out{.grid = grid};'
"" | Out-File "$scr\mutation-transient4-progress.txt"
foreach ($e in $m) {
    $name = $e[0]
    if (Test-Path "$scr\mutation-$name.log") { $prev = Get-Content "$scr\mutation-$name.log" -Raw; if ($prev -match 'VERDICT') { continue } }
    & "$scr\mutate-t.ps1" -Name $name -File $e[2] -Find $e[4] -Replace $e[5] -Filter $e[3] | Out-Null
    $v = (Get-Content "$scr\mutation-$name.log" | Select-String "VERDICT|BUILD FAILED|target text was not found").Line -join ' '
    $t = (Get-Content "$scr\mutation-$name.log" | Select-String "^TESTS").Line
    "$name | $($e[1]) | $v | $t" | Out-File "$scr\mutation-transient4-progress.txt" -Append
}
"DONE" | Out-File "$scr\mutation-transient4-progress.txt" -Append
