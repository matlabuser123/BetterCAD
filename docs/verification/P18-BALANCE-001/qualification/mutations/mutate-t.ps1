# One mutation probe with a timeout on the test run (a mutation that removes a resource guard can turn a refusal into a run that
# never ends; a test that hangs has failed). Usage: mutate-t.ps1 -Name M1 -File <path> -Find <text> -Replace <text> -Filter "<catch2 tags>"
param(
    [string]$Name,
    [string]$File,
    [string]$Find,
    [string]$Replace,
    [string]$Filter,
    [int]$TimeoutSeconds = 240
)
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:Path = "$mingw;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$log = "$s\mutation-$Name.log"
$original = [IO.File]::ReadAllText($File)
if (-not $original.Contains($Find)) {
    "MUTATION $Name : the target text was not found in $File" | Tee-Object -FilePath $log
    exit 3
}
$mutated = $original.Replace($Find, $Replace)
[IO.File]::WriteAllText($File, $mutated, (New-Object Text.UTF8Encoding $false))
"MUTATION $Name applied to $File" | Out-File $log
"  find:    $Find" | Out-File $log -Append
"  replace: $Replace" | Out-File $log -Append
try {
    Set-Location "C:\Users\hasib\OneDrive\Desktop\CAD"
    $buildOut = & cmake --build --preset debug-ext --target bettercad_tests 2>&1
    $buildCode = $LASTEXITCODE
    $buildOut | Out-File "$s\mutation-$Name-build.log"
    if ($buildCode -ne 0) {
        "BUILD FAILED (exit $buildCode) -- DID NOT COMPILE, VOID" | Out-File $log -Append
    } else {
        $exe = "C:\Users\hasib\bcdev\bc-build\debug-ext\bin\bettercad_tests.exe"
        $out = "$s\mutation-$Name-tests.log"
        $proc = Start-Process -FilePath $exe -ArgumentList $Filter -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError "$s\mutation-$Name-tests.err"
        $finished = $proc.WaitForExit($TimeoutSeconds * 1000)
        if (-not $finished) {
            try { $proc.Kill() } catch {}
            "TESTS TIMED OUT after $TimeoutSeconds s" | Out-File $log -Append
            "VERDICT: KILLED (timeout)" | Out-File $log -Append
        } else {
            # The verdict is read from the test output, never from ExitCode: Start-Process can leave ExitCode empty.
            $text = Get-Content $out -Raw
            $summary = (Get-Content $out | Select-String -Pattern "^test cases:|^assertions:|^All tests passed").Line -join " | "
            "TESTS : $summary" | Out-File $log -Append
            if ($text -match "All tests passed") { "VERDICT: SURVIVED" | Out-File $log -Append }
            elseif ($text -match "test cases:\s+\d+ \|\s+\d+ passed \|\s+\d+ failed") { "VERDICT: KILLED" | Out-File $log -Append }
            else { "VERDICT: UNKNOWN (no summary line)" | Out-File $log -Append }
        }
    }
} finally {
    [IO.File]::WriteAllText($File, $original, (New-Object Text.UTF8Encoding $false))
    "restored $File" | Out-File $log -Append
}
Get-Content $log
