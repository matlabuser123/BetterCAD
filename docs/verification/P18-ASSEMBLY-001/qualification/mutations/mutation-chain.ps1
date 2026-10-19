$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
while (-not ((Test-Path "$s\mutation-driver-progress.txt") -and ((Get-Content "$s\mutation-driver-progress.txt" -Raw) -match 'DONE'))) { Start-Sleep 20 }
Copy-Item "$s\mutation-driver-progress.txt" "$s\mutation-driver-progress-2.txt"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutation-driver.ps1"
Copy-Item "$s\mutation-driver-progress.txt" "$s\mutation-driver-progress-3.txt"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutation-driver2.ps1"
