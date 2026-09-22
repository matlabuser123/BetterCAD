# CTest parallelism benchmark.
#
#   .\benchmark-tests.ps1 -Preset debug -Jobs 8,12,16 -Repeats 2
#
# Runs the WHOLE suite at each -j, twice by default, and records runtime and
# every failure. Two passes matter more than one: a concurrency level that is
# fast but flaky shows up as a failure in one pass and not the other, and the
# point of this benchmark is to reject exactly that rather than to find the
# smallest number.
#
# Run under code page 65001. cli.new.unicode-path fails spuriously under 437,
# which would otherwise look like a concurrency failure and be attributed to
# the wrong cause.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Preset,
    [int[]]$Jobs = @(8, 12, 16),
    [int]$Repeats = 2
)

$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here "..\..\..")
$results = Join-Path $here "test-results.csv"
$scratch = Join-Path $here "scratch"
if (-not (Test-Path $scratch)) { New-Item -ItemType Directory $scratch | Out-Null }
if (-not (Test-Path $results)) {
    "preset,jobs,pass,seconds,total,passed,failed,exit" | Out-File -FilePath $results -Encoding utf8
}

Set-Location $repo
# The unicode CLI test needs UTF-8; without it a pass looks like a failure.
$null = cmd /c "chcp 65001"

foreach ($j in $Jobs) {
    for ($pass = 1; $pass -le $Repeats; $pass++) {
        $log = Join-Path $scratch "ctest-$Preset-j$j-pass$pass.log"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        & ctest --preset $Preset -j $j *> $log
        $code = $LASTEXITCODE
        $sw.Stop()
        $seconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)

        # Parsed from the log, never inferred: "100% tests passed out of N".
        $total = 0; $passed = 0; $failed = 0
        $line = Select-String -Path $log -Pattern "tests passed out of" | Select-Object -Last 1
        if ($line -and $line.Line -match "(\d+)%\s+tests passed out of\s+(\d+)") {
            $total = [int]$Matches[2]
            $passed = [int][math]::Round($total * [int]$Matches[1] / 100.0)
            $failed = $total - $passed
        }
        $m = Select-String -Path $log -Pattern "^\s*(\d+) tests failed out of" | Select-Object -Last 1
        if ($m -and $m.Line -match "^\s*(\d+) tests failed") { $failed = [int]$Matches[1] }

        "$Preset,$j,$pass,$seconds,$total,$passed,$failed,$code" |
            Out-File -FilePath $results -Encoding utf8 -Append
        $verdict = if ($code -eq 0) { "clean" } else { "FAILURES" }
        "  -j {0,-3} pass {1}  {2,8:N2}s  {3}/{4} passed  {5}" -f $j, $pass, $seconds, $passed, $total, $verdict |
            Write-Host
        if ($code -ne 0) {
            # Named, so a concurrency failure is attributable rather than a number.
            Select-String -Path $log -Pattern "^\s+\d+ - " | ForEach-Object { "      $($_.Line.Trim())" } | Write-Host
        }
    }
}
Write-Host ""
Write-Host "results appended to $results"
