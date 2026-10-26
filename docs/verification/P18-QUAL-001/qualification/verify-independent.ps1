# P18-QUAL-001 independent analytical verification of FRESH CLI payloads.
#
# The reference-model workflow (cli.thermal.* / refmod.thermal.workflow) leaves, in each preset's build tree, the raw stdout of a real bettercad-cli
# process for every command it runs on the committed thermal reference documents. This script reads those files with a JSON parser that shares no code with
# BetterCAD and compares them with CLOSED FORMS written below from the hand constants of docs/verification/P18-REFMOD-001/RM-THERM-0x.md. Nothing here
# includes, links or calls BetterCAD; the only input is bytes a process wrote. (Python is not installed on this machine, so this is PowerShell.)
#
#   verify-independent.ps1 -Preset release-ext        exit code = the number of failed checks
#
# ASCII ONLY: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
param([string]$Preset = "release-ext", [string]$BuildRoot = "C:\Users\hasib\bcdev\bc-build", [string]$PayloadDir = "")
$dir = if ($PayloadDir -ne "") { $PayloadDir } else { Join-Path $BuildRoot "$Preset\tests\cli-output\thermal-refmod-workflow\payloads" }
$failures = 0
$rows = @()
function Load([string]$name) { return Get-Content (Join-Path $dir $name) -Raw | ConvertFrom-Json }
function Check([string]$model, [string]$what, [double]$got, [double]$expected, [double]$absTol) {
    $err = [Math]::Abs($got - $expected)
    $ok = $err -le $absTol
    if (-not $ok) { $script:failures++ }
    $script:rows += ("{0,-8} {1,-52} got {2,-24} expected {3,-22} |err| {4,-10:E2} tol {5,-8:E0} {6}" -f $model, $what, $got.ToString("R"), $expected.ToString("R"), $err, $absTol, $(if ($ok) { "PASS" } else { "FAIL" }))
}
function CheckText([string]$model, [string]$what, [string]$got, [string]$expected) {
    $ok = $got -eq $expected
    if (-not $ok) { $script:failures++ }
    $script:rows += ("{0,-8} {1,-52} got {2,-24} expected {3,-22} {4}" -f $model, $what, $got, $expected, $(if ($ok) { "PASS" } else { "FAIL" }))
}

"P18-QUAL-001 independent verification, preset $Preset, payload directory $dir"
if (-not (Test-Path $dir)) { "MISSING payload directory"; exit 99 }

# ---- RM-THERM-02: steady slab, 500 x 500 x 1000 mm, k = 8, 400 K to 300 K.  Q = k A dT / L = 8 * 0.25 * 100 / 1 = 200 W; q = 800 W/m^2.
$k = 8.0; $A = 0.5 * 0.5; $L = 1.0; $dT = 100.0
$Q = $k * $A * $dT / $L; $q = $k * $dT / $L
$s = Load "rm02.solve.json"
CheckText "RM02" "mode" $s.mode "steady"
Check "RM02" "minimum temperature [K] (cold cap)" $s.temperature.min.value 300.0 1e-9
Check "RM02" "maximum temperature [K] (hot cap)" $s.temperature.max.value 400.0 1e-9
Check "RM02" "maximum heat flux [W/m^2] = k dT / L" $s.validation.metrics.maximumHeatFlux.value $q 1e-6
$h = Load "rm02.heatflux.json"
Check "RM02" "smallest element |q| [W/m^2]" $h.magnitude.min $q 1e-6
Check "RM02" "largest element |q| [W/m^2]" $h.magnitude.max $q 1e-6
Check "RM02" "heat flux z of the largest element [W/m^2]" $h.largest.flux.z $q 1e-6
Check "RM02" "heat flux x of the largest element [W/m^2]" $h.largest.flux.x 0.0 1e-6
$b = Load "rm02.balance.json"
Check "RM02" "Dirichlet reaction into the body [W] (sum of both caps)" $b.final.dirichletReactionInto.value 0.0 1e-9
Check "RM02" "surface + generation + convection [W]" ($b.final.surfaceHeatInto.value + $b.final.generationInto.value + $b.final.convectionInto.value) 0.0 1e-12
CheckText "RM02" "balanced" ([string]$b.balanced) "True"
$s2 = Load "rm02.solve-again.json"
CheckText "RM02" "a second fresh process gives the same minimum temperature (text)" ([string]$s2.temperature.min.value) ([string]$s.temperature.min.value)

# ---- RM-THERM-02b: surface flux. 50 x 50 x 100 mm, k = 5, start cap held 300 K, +500 W/m^2 into the body through the end cap.
$kf = 5.0; $Af = 0.05 * 0.05; $Lf = 0.1; $qs = 500.0
$Qf = $qs * $Af; $dTdz = $qs / $kf; $Tend = 300.0 + $dTdz * $Lf
$sf = Load "rm02b.solve.json"
Check "RM02b" "maximum temperature [K] = 300 + (q''/k) L" $sf.temperature.max.value $Tend 1e-9
Check "RM02b" "minimum temperature [K] (held cap)" $sf.temperature.min.value 300.0 1e-9
Check "RM02b" "maximum heat flux [W/m^2] = q''" $sf.validation.metrics.maximumHeatFlux.value $qs 1e-6
$bf = Load "rm02b.balance.json"
Check "RM02b" "surface heat into the body [W] = q'' A" $bf.final.surfaceHeatInto.value $Qf 1e-12
Check "RM02b" "Dirichlet reaction into the body [W] = -q'' A" $bf.final.dirichletReactionInto.value (-$Qf) 1e-12

# ---- RM-THERM-05: conduction in series with convection. A = 0.4 * 0.5 = 0.2 m^2, L = 0.5 m, k = 10, 400 K held, h = 20 to 300 K.
$k5 = 10.0; $A5 = 0.4 * 0.5; $L5 = 0.5; $h5 = 20.0; $Th = 400.0; $Tinf = 300.0
$Rcond = $L5 / ($k5 * $A5); $Rconv = 1.0 / ($h5 * $A5); $Q5 = ($Th - $Tinf) / ($Rcond + $Rconv); $Ts = $Tinf + $Q5 * $Rconv
$s5 = Load "rm05.solve.json"
Check "RM05" "surface temperature = minimum temperature [K]" $s5.temperature.min.value $Ts 1e-9
Check "RM05" "maximum temperature [K] (held cap)" $s5.temperature.max.value $Th 1e-9
Check "RM05" "maximum heat flux [W/m^2] = Q / A" $s5.validation.metrics.maximumHeatFlux.value ($Q5 / $A5) 1e-6
$b5 = Load "rm05.balance.json"
Check "RM05" "Dirichlet reaction into the body [W] = +Q" $b5.final.dirichletReactionInto.value $Q5 1e-9
Check "RM05" "convection into the body [W] = -Q" $b5.final.convectionInto.value (-$Q5) 1e-9
Check "RM05" "convecting area [m^2]" $b5.convections[0].area.value $A5 1e-12

# ---- RM-THERM-08: lumped transient. rho c V = 220.7744 J/K, h = 25, A = 9.6e-3 m^2, T0 = 400 K, Tinf = 300 K, dt = 100 s, 46 steps.
# Backward Euler on a lumped body: T[n+1] = (T[n] + (dt/tau) Tinf) / (1 + dt/tau), tau = rho c V / (h A). The mesh has a small spatial
# non-uniformity, so the mean of the stored minimum and maximum is compared (tolerance 0.05 K), exactly as the reference suite does against its own oracle.
$rcV = 220.7744; $h8 = 25.0; $A8 = 9.6e-3; $tau = $rcV / ($h8 * $A8); $dt = 100.0
$t8 = Load "rm08.temperature.json"
CheckText "RM08" "mode" $t8.mode "transient"
$T = 400.0; $worst = 0.0; $n = 0
foreach ($entry in $t8.history) {
    if ($entry.index -gt 0) { $T = ($T + ($dt / $tau) * 300.0) / (1.0 + $dt / $tau) }
    $mean = 0.5 * ($entry.min.value + $entry.max.value)
    $d = [Math]::Abs($mean - $T); if ($d -gt $worst) { $worst = $d }
    $n++
}
Check "RM08" "worst |mean(min,max) - lumped backward Euler| over the history [K]" $worst 0.0 0.05
Check "RM08" "stored states" $n 47.0 0.0
Check "RM08" "time of the last stored state [s]" $t8.state.time.value 4600.0 0.0
Check "RM08" "time constant tau [s] (hand: 919.893)" $tau 919.893 1e-3

""
$rows
""
"checks failed: $failures"
exit $failures
