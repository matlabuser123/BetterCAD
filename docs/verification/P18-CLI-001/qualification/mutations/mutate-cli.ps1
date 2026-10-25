# One mutation probe for the thermal CLI. Applies the edit, rebuilds bettercad_tests, runs the thermal CLI and run suites, runs the
# ownership gate, and ALWAYS restores the file. A probe is KILLED if the tests fail OR the gate fails; the log says which.
param(
    [string]$Name,
    [string]$File,
    [string]$FindFile,
    [string]$ReplaceFile,
    [string]$Filter = "ThermalCli_*,ThermalRun_*",
    [int]$TimeoutSeconds = 900
)
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$log = "$s\mutation-$Name.log"
$Find = [IO.File]::ReadAllText($FindFile)
$Replace = [IO.File]::ReadAllText($ReplaceFile)
$original = [IO.File]::ReadAllText($File)
if (-not $original.Contains($Find)) {
    "MUTATION $Name : the target text was not found in $File" | Tee-Object -FilePath $log
    exit 3
}
[IO.File]::WriteAllText($File, $original.Replace($Find, $Replace), (New-Object Text.UTF8Encoding $false))
"MUTATION $Name applied to $File" | Out-File $log
try {
    # THE GATE FIRST: it is a source-text check and needs no build.
    $gateOut = & cmake "-DSOURCE_DIR=$root" -P "$root\tests\architecture\CheckThermalCliOwnership.cmake" 2>&1 | Out-String
    $gateFailed = $LASTEXITCODE -ne 0
    $gateWhat = if ($gateFailed) { ([regex]::Match($gateOut, 'failed: (\d+) violation')).Groups[1].Value + " violation(s)" } else { "passes" }
    "GATE : $gateWhat" | Out-File $log -Append
    Set-Location $root
    $buildOut = & cmake --build --preset debug-ext --target bettercad_tests 2>&1
    $buildCode = $LASTEXITCODE
    $buildOut | Out-File "$s\mutation-$Name-build.log"
    if ($buildCode -ne 0) {
        "BUILD FAILED (exit $buildCode) -- DID NOT COMPILE" | Out-File $log -Append
        if ($gateFailed) { "VERDICT: KILLED by the gate (the mutant does not compile either)" | Out-File $log -Append } else { "VERDICT: VOID" | Out-File $log -Append }
    } else {
        $exe = "C:\Users\hasib\bcdev\bc-build\debug-ext\bin\bettercad_tests.exe"
        $out = "$s\mutation-$Name-tests.log"
        $proc = Start-Process -FilePath $exe -ArgumentList $Filter -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError "$s\mutation-$Name-tests.err"
        $finished = $proc.WaitForExit($TimeoutSeconds * 1000)
        $testsKilled = $false
        if (-not $finished) {
            try { $proc.Kill() } catch {}
            "TESTS TIMED OUT after $TimeoutSeconds s" | Out-File $log -Append
            $testsKilled = $true
        } else {
            $text = Get-Content $out -Raw
            $summary = (Get-Content $out | Select-String -Pattern "^test cases:|^assertions:|^All tests passed").Line -join " | "
            "TESTS : $summary" | Out-File $log -Append
            $testsKilled = -not ($text -match "All tests passed")
        }
        $by = @()
        if ($testsKilled) { $by += "tests" }
        if ($gateFailed) { $by += "gate" }
        if ($by.Count -gt 0) { "VERDICT: KILLED by $($by -join ' and ')" | Out-File $log -Append } else { "VERDICT: SURVIVED" | Out-File $log -Append }
    }
} finally {
    [IO.File]::WriteAllText($File, $original, (New-Object Text.UTF8Encoding $false))
}
