$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
while (-not ((Test-Path "$s\mutation-driver2-progress.txt") -and ((Get-Content "$s\mutation-driver2-progress.txt" -Raw) -match 'DONE'))) { Start-Sleep 20 }
$SYS = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalSystem.cpp"
$f1 = 'conductanceValues[*slot] += (*ke)(a, b).si();'
$r1 = 'if (b >= a) { conductanceValues[*slot] += (*ke)(a, b).si(); }'
$f2 = ("conductanceLayout.rowStart, conductanceLayout.inner, std::move(conductanceValues),`n        kSymmetryTolerance);")
$r2 = ("conductanceLayout.rowStart, conductanceLayout.inner, std::move(conductanceValues));")
& "$s\mutate-multi.ps1" -Name C01 -File $SYS -Find @($f1,$f2) -Replace @($r1,$r2) -Filter "[thermal][assembly]" | Out-Null
$v = (Get-Content "$s\mutation-C01.log" | Select-String "VERDICT|BUILD FAILED|target text").Line -join ' '
$t = (Get-Content "$s\mutation-C01.log" | Select-String "^TESTS").Line
"C01 | upper-only Ke scatter with the symmetry gate removed | $v | $t" | Out-File "$s\mutation-driver3-progress.txt"
"DONE" | Out-File "$s\mutation-driver3-progress.txt" -Append
