# Qualification performance benchmark.
#
# Times a CLEAN build of one preset at a given parallelism, in a given build
# root, and appends rows to results.csv. Nothing about the build itself
# changes: same generator, same compiler, same flags, same warnings-as-errors,
# same sources. Only -j and the build directory vary.
#
#   .\benchmark.ps1 -Preset debug -Jobs 12 -Label sweep-j12
#   .\benchmark.ps1 -Preset debug -Jobs 12 -Label local-j12 -BuildRoot C:\Dev\bc
#
# Every run is a full clean, because a warm incremental build would measure
# nothing that qualification actually pays for.
#
# Real exit codes are captured immediately after each command and recorded,
# so a failed stage is visible in the results rather than masked -- the
# P13-XFORM-001 lesson applies to benchmark harnesses too.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Preset,
    [Parameter(Mandatory = $true)][int]$Jobs,
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$BuildRoot = "",
    [switch]$SkipRebuild
)

$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here "..\..\..")
$results = Join-Path $here "results.csv"
$scratch = Join-Path $here "scratch"
if (-not (Test-Path $scratch)) { New-Item -ItemType Directory $scratch | Out-Null }
if (-not (Test-Path $results)) {
    "label,preset,jobs,build_dir,stage,seconds,exit,warnings" | Out-File -FilePath $results -Encoding utf8
}

Set-Location $repo

if ($BuildRoot -eq "") {
    $buildDir = Join-Path $repo "build\$Preset"
    $configureArgs = @("--preset", $Preset)
} else {
    $buildDir = Join-Path $BuildRoot $Preset
    $configureArgs = @("--preset", $Preset, "-B", $buildDir)
}

function Invoke-Stage {
    param([string]$Stage, [string]$Exe, [string[]]$Arguments)

    $log = Join-Path $scratch "$Label-$Preset-$Stage.log"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $Exe @Arguments *> $log
    $code = $LASTEXITCODE
    $sw.Stop()
    $seconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)

    # Counted, never inferred from the exit code: the 0-warning gate is a
    # measurement of the log.
    $warnings = 0
    if (Test-Path $log) {
        $warnings = @(Select-String -Path $log -Pattern "warning" -SimpleMatch -CaseSensitive:$false).Count
    }

    "$Label,$Preset,$Jobs,$buildDir,$Stage,$seconds,$code,$warnings" |
        Out-File -FilePath $results -Encoding utf8 -Append
    $status = if ($code -eq 0) { "ok" } else { "FAILED" }
    "  {0,-10} {1,8:N2}s  exit {2} ({3})  warnings {4}" -f $Stage, $seconds, $code, $status, $warnings | Write-Host
    return $code
}

Write-Host "=== $Label : $Preset, -j $Jobs, $buildDir ==="

if ((Invoke-Stage -Stage "configure" -Exe "cmake" -Arguments $configureArgs) -ne 0) {
    Write-Host "  configure failed; not building"
    exit 1
}
# Clean is not optional and not timed away: it is what makes this measure a
# qualification build rather than an incremental one.
if ((Invoke-Stage -Stage "clean" -Exe "cmake" -Arguments @("--build", $buildDir, "--target", "clean")) -ne 0) {
    Write-Host "  clean failed; not building"
    exit 1
}
if ((Invoke-Stage -Stage "build" -Exe "cmake" -Arguments @("--build", $buildDir, "--parallel", "$Jobs")) -ne 0) {
    Write-Host "  build failed"
    exit 1
}
if (-not $SkipRebuild) {
    # The freshness proof, timed so its cost is known rather than assumed.
    Invoke-Stage -Stage "rebuild" -Exe "cmake" -Arguments @("--build", $buildDir, "--parallel", "$Jobs") | Out-Null
}
Write-Host ""
