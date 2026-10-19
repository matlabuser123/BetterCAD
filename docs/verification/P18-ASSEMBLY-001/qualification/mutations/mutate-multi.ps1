# One mutation probe: apply a textual mutation to a file, build, run a test
# filter, record, restore. Usage: mutate.ps1 -Name M1 -File <path> -Find <text> -Replace <text> -Filter "<catch2 tags or ctest regex>" [-Ctest]
param(
    [string]$Name,
    [string]$File,
    [string[]]$Find,
    [string[]]$Replace,
    [string]$Filter,
    [switch]$Ctest
)
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$log = "$s\mutation-$Name.log"
$original = [IO.File]::ReadAllText($File)
for ($i = 0; $i -lt $Find.Length; $i++) { if (-not $original.Contains($Find[$i])) {
    "MUTATION $Name : the target text was not found in $File" | Tee-Object -FilePath $log
    exit 3
} }
$mutated = $original
for ($i = 0; $i -lt $Find.Length; $i++) { $mutated = $mutated.Replace($Find[$i], $Replace[$i]) }
[IO.File]::WriteAllText($File, $mutated, (New-Object Text.UTF8Encoding $false))
"MUTATION $Name applied to $File" | Out-File $log
"  find:    $($Find -join " ;; ")" | Out-File $log -Append
"  replace: $($Replace -join " ;; ")" | Out-File $log -Append
try {
    Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
    $buildOut = & cmake --build --preset debug-ext --target bettercad_tests 2>&1
    $buildCode = $LASTEXITCODE
    $buildOut | Out-File "$s\mutation-$Name-build.log"
    if ($buildCode -ne 0) {
        "BUILD FAILED (exit $buildCode) -- DID NOT COMPILE, VOID" | Out-File $log -Append
    } else {
        if ($Ctest) {
            $testOut = & ctest --test-dir "C:\Users\hasib\bcdev\bc-build\debug-ext" -R $Filter -j 4 2>&1
            $code = $LASTEXITCODE
            $testOut | Out-File "$s\mutation-$Name-tests.log"
            $summary = ($testOut | Select-String -Pattern "tests passed|Failed").Line -join " | "
        } else {
            $testOut = & "C:\Users\hasib\bcdev\bc-build\debug-ext\bin\bettercad_tests.exe" $Filter 2>&1
            $code = $LASTEXITCODE
            $testOut | Out-File "$s\mutation-$Name-tests.log"
            $summary = ($testOut | Select-String -Pattern "^test cases:|^assertions:|^All tests passed").Line -join " | "
        }
        "TESTS exit $code : $summary" | Out-File $log -Append
        if ($code -eq 0) { "VERDICT: SURVIVED" | Out-File $log -Append } else { "VERDICT: KILLED" | Out-File $log -Append }
    }
} finally {
    [IO.File]::WriteAllText($File, $original, (New-Object Text.UTF8Encoding $false))
    "restored $File" | Out-File $log -Append
}
Get-Content $log
