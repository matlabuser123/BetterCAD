# P19-MESH-001 qualification preflight: FREE DISK SPACE.
#
# WHY THIS EXISTS. Attempt 1 (2026-10-28) died in all three builds with
#
#     fatal error: error writing to C:\Users\hasib\AppData\Local\Temp\cc....s:
#                  No space left on device
#
# The volume holding %TEMP% was full. run-qualification.cmd restarts the OneDrive
# client and checks its memory before launching -- the ritual INFRA-VIEWER-001 and
# P19-ARCH-001 left behind -- but NOTHING checked disk, so a run that needs about
# seven hours was launched and could not write its 131st object file. Five of
# seventeen stages failed, no suite ran, and the two determinism stages measured a
# build tree the clean had just emptied. This refuses to start that run instead.
#
# IT MEASURES AND REPORTS; IT DOES NOT INVENT A REQUIREMENT. FREEZE.md declines to
# claim a figure for the space the qualification needs -- "the previous passing run
# (P19-FLUID-001) built the same three trees on the same volume, so the space that
# sufficed then is the baseline" -- and this script will not claim one either. The
# threshold is QUALIFY_MIN_FREE_GB, set in run-qualification.cmd by the owner. If it
# is unset this REFUSES rather than guessing: nobody should start a seven-hour run
# on an unexamined disk budget. The report below is what calibrates it, so set it
# from the next PASSING run's measured figures and not from an estimate.
#
# ASCII ONLY, AND NO BOM: Windows PowerShell 5.1 reads a BOM-less script as ANSI,
# the same constraint freeze-mesh.ps1 carries.
#
# Exit codes
#     0   every distinct volume has at least QUALIFY_MIN_FREE_GB free
#     1   at least one volume is below it -- do not launch
#     9   QUALIFY_MIN_FREE_GB is unset or not a positive whole number
#
# Standalone use (safe: this script never hard-stops a console; only the .cmd
# wrapper does, and only when the harness calls it):
#
#     powershell -NoProfile -ExecutionPolicy Bypass -File preflight-space.ps1 -WithSizes
#
param(
    [switch]$WithSizes,
    [string]$SourceTree = "C:\Users\Hasib\Desktop\CAD"
)
$ErrorActionPreference = "Stop"

function VolumeOf([string]$path) {
    return [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($path))
}
function Gb([long]$bytes) {
    return [math]::Round($bytes / 1GB, 2)
}
function DirSizeGb([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $sum = 0L
    Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue |
        ForEach-Object { $sum += $_.Length }
    return (Gb $sum)
}

"== P19-MESH-001 preflight: free disk space =="
"   " + (Get-Date -Format "ddd dd/MM/yyyy HH:mm:ss")
""

# --- the threshold, which the owner sets and this script will not default -----
$raw = $env:QUALIFY_MIN_FREE_GB
$min = 0
if ([string]::IsNullOrWhiteSpace($raw) -or -not [int]::TryParse($raw, [ref]$min) -or $min -le 0) {
    "REFUSED: QUALIFY_MIN_FREE_GB is not set to a positive whole number of GB."
    "         It is currently: [" + $raw + "]"
    ""
    "         Set it in run-qualification.cmd before launching. This script will"
    "         not supply a default: FREEZE.md claims no figure for the space the"
    "         qualification needs, and a guess here would either refuse a run that"
    "         would have passed or admit one that cannot finish."
    ""
    "         To calibrate it, run this script with -WithSizes after the next"
    "         PASSING qualification and read the build root's measured size below."
    exit 9
}
"threshold   QUALIFY_MIN_FREE_GB = " + $min + " GB, required free on each volume below"
""

# --- the volumes that matter --------------------------------------------------
$targets = @()
$targets += [pscustomobject]@{ What = "%TEMP% (the compiler's cc*.s files)"; Path = $env:TEMP }
if (-not [string]::IsNullOrWhiteSpace($env:BETTERCAD_BUILD_ROOT)) {
    $targets += [pscustomobject]@{ What = "BETTERCAD_BUILD_ROOT (the three build trees)"; Path = $env:BETTERCAD_BUILD_ROOT }
} else {
    "note        BETTERCAD_BUILD_ROOT is unset, so its volume was NOT checked."
    ""
}

$seen = @{}
$fail = @()
foreach ($t in $targets) {
    $t.What
    "    path    " + $t.Path
    $vol = VolumeOf $t.Path
    $drive = New-Object System.IO.DriveInfo $vol
    $free = Gb $drive.AvailableFreeSpace
    $total = Gb $drive.TotalSize
    "    volume  " + $vol + "   free " + $free + " GB of " + $total + " GB"
    if ($seen.ContainsKey($vol)) {
        "    the same volume as " + $seen[$vol] + " -- counted once"
    } else {
        $seen[$vol] = $t.What
        if ($drive.AvailableFreeSpace -lt ([long]$min * 1GB)) {
            "    *** BELOW THE THRESHOLD: " + $free + " GB free, " + $min + " GB required ***"
            $fail += ($vol + " (" + $free + " GB free, " + $min + " GB required)")
        } else {
            "    OK"
        }
    }
    ""
}

# --- what is safe to remove, from FREEZE.md's storage note --------------------
"removable if a volume is short. NOTHING under docs/verification/ is removable:"
$cands = @(
    [pscustomobject]@{ What = "the in-tree build/ (stale CMake trees, git-ignored, unused by the harness)"; Path = (Join-Path $SourceTree "build") },
    [pscustomobject]@{ What = "bettercad-qualify-index (tiny, recreated by the harness)"; Path = (Join-Path $env:TEMP "bettercad-qualify-index") },
    [pscustomobject]@{ What = "bettercad-harness-check (tiny, recreated by verify-harness.cmd)"; Path = (Join-Path $env:TEMP "bettercad-harness-check") }
)
foreach ($c in $cands) {
    $present = Test-Path -LiteralPath $c.Path
    $size = ""
    if ($present -and $WithSizes) {
        $g = DirSizeGb $c.Path
        if ($null -ne $g) { $size = "   " + $g + " GB" }
    }
    $mark = "absent "
    if ($present) { $mark = "present" }
    "    [" + $mark + "] " + $c.What + $size
    "              " + $c.Path
}
$orphans = @(Get-ChildItem -LiteralPath $env:TEMP -Filter "cc*.s" -File -Force -ErrorAction SilentlyContinue)
if ($orphans.Count -gt 0) {
    $og = Gb (($orphans | Measure-Object -Property Length -Sum).Sum)
    "    [present] " + $orphans.Count + " orphaned cc*.s files in %TEMP% from aborted compiles   " + $og + " GB"
} else {
    "    [absent ] no orphaned cc*.s files in %TEMP%"
}
""

# --- the build root's own size, which calibrates the threshold ----------------
if (-not [string]::IsNullOrWhiteSpace($env:BETTERCAD_BUILD_ROOT)) {
    if ($WithSizes) {
        $bg = DirSizeGb $env:BETTERCAD_BUILD_ROOT
        if ($null -eq $bg) {
            "build root  does not exist yet, so it has no size to report."
        } else {
            "build root  " + $bg + " GB occupied now. AFTER A PASSING RUN this is the"
            "            measured size of three complete trees, and the figure to set"
            "            QUALIFY_MIN_FREE_GB from, with headroom for %TEMP%."
        }
    } else {
        "build root  size not measured. Pass -WithSizes for it; it enumerates every"
        "            file and takes minutes on a populated tree."
    }
    ""
}

# --- verdict -----------------------------------------------------------------
if ($fail.Count -gt 0) {
    "RESULT: REFUSED. Do not launch the qualification."
    foreach ($f in $fail) { "        short: " + $f }
    ""
    "        Free space on the volume above, then run this again. Attempt 1 failed"
    "        for exactly this reason and cost a seven-hour slot."
    exit 1
}
"RESULT: OK. Every distinct volume has at least " + $min + " GB free."
exit 0
