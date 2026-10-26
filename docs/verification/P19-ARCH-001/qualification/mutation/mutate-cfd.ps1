# One mutation probe for the thermal reference suite. Applies the edit, rebuilds bettercad_tests (release-ext), runs the reference suite
# ([refmod][thermal]), and ALWAYS restores the file. A probe is KILLED if the suite fails (or the mutant does not compile -> VOID).
param(
    [string]$Name,
    [string]$File,
    [string]$FindFile,
    [string]$ReplaceFile,
    [string]$Filter = "[refmod][thermal]",
    [int]$TimeoutSeconds = 900
)
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$pk = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$env:Path = "$pk\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$pk\Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe\cmd;$env:Path"
$env:BETTERCAD_BUILD_ROOT = "C:/Users/hasib/bcdev/bc-build"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"
$log = "$s\mutation-cfd-$Name.log"
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
    Set-Location $root
    $buildOut = & cmake --build "C:\Users\hasib\bcdev\bc-build\release-ext" --target bettercad_tests 2>&1
    $buildCode = $LASTEXITCODE
    $buildOut | Out-File "$s\mutation-cfd-$Name-build.log"
    if ($buildCode -ne 0) {
        "BUILD FAILED (exit $buildCode) -- DID NOT COMPILE" | Out-File $log -Append
        "VERDICT: VOID" | Out-File $log -Append
    } else {
        $out = "$s\mutation-cfd-$Name-tests.log"
        $ctestOut = & ctest --test-dir "C:\Users\hasib\bcdev\bc-build\release-ext" -R "^(unit\.CfdInput|architecture\.)" -j 6 --timeout 600 2>&1
        $ctestOut | Out-File $out -Encoding utf8
        $text = ($ctestOut | Out-String)
        $failedCases = ($ctestOut | Select-String -Pattern "^\s*\d+ - unit\.(\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join "; "
        $summary = ($ctestOut | Select-String -Pattern "tests passed, \d+ tests failed|tests passed out of").Line -join " | "
        "TESTS : $summary" | Out-File $log -Append
        "FAILED CASES : $failedCases" | Out-File $log -Append
        $killed = -not ($text -match "100% tests passed")
        if ($killed) { "VERDICT: KILLED by tests" | Out-File $log -Append } else { "VERDICT: SURVIVED" | Out-File $log -Append }
    }
} finally {
    [IO.File]::WriteAllText($File, $original, (New-Object Text.UTF8Encoding $false))
}


