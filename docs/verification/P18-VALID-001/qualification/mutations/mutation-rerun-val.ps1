# Re-runs the probes of the first campaign that did not give a clean KILLED (a survivor now covered by a new test, a find text that was not
# applied, a mutation that did not compile), plus three probes the first campaign did not have. Strings are single-quoted: no escaping.
$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"
$cpp = "C:\Users\hasib\OneDrive\Desktop\CAD\src\thermal\ThermalValidation.cpp"
$progress = "$s\val-rerun-progress.txt"
"rerun started $(Get-Date -Format s)" | Out-File $progress
$probes = @(
    @{ Id = 'VAL11r'; Desc = 'invalid elements are not reported (now with a test on the count alone)'; Find = '    if (quality.invalidElements > 0) {'; Replace = '    if (false) {' },
    @{ Id = 'VAL26r'; Desc = 'a solution from another assembled system is accepted (now named for the Solution)'; Find = 'if (derived.system != nullptr && !(steady->source().system == derived.system->source())) {'; Replace = 'if (false) {' },
    @{ Id = 'VAL28r'; Desc = 'recovered fields of another solution are accepted (now named for the PostResult)'; Find = 'if (solutionSource.has_value() && !(derived.post->source() == *solutionSource)) {'; Replace = 'if (false) {' },
    @{ Id = 'VAL27r'; Desc = 'a steady solution for a transient analysis is not a mode mismatch'; Find = 'wrongMode(ValidationSubject::Solution, "a steady solution was presented for a transient analysis");'; Replace = '(void)0;' },
    @{ Id = 'VAL31r'; Desc = 'no free equation (fully constrained) is a failure'; Find = 'if (equations.freeCount() == 0) {'; Replace = 'if (equations.freeCount() == 0) { findings.push_back(make(ValidationCode::NoTemperatureDatum, "no free equation"));' },
    @{ Id = 'VAL45r'; Desc = 'a missing element result is not a cardinality failure'; Find = 'if (!cardinalityOk) {'; Replace = 'if (!cardinalityOk && false) {' },
    @{ Id = 'VAL76'; Desc = 'a balance of another solution is accepted'; Find = 'if (solutionSource.has_value() && !(derived.balance->source() == *solutionSource)) {'; Replace = 'if (false) {' },
    @{ Id = 'VAL77'; Desc = 'a history from another assembled system is accepted'; Find = 'if (derived.system != nullptr && !(transient->source().system == derived.system->source())) {'; Replace = 'if (false) {' },
    @{ Id = 'VAL78'; Desc = 'prepared loads of another list than the system are accepted'; Find = 'if (derived.preparedLoads != nullptr && !(derived.preparedLoads->source().loads == derived.system->source().loads)) {'; Replace = 'if (false) {' }
)
foreach ($p in $probes) {
    $t = Get-Date
    & "$s\mutate-t.ps1" -Name $p.Id -File $cpp -Find $p.Find -Replace $p.Replace -Filter '[thermal][validation]' -TimeoutSeconds 420 | Out-Null
    $log = Get-Content "$s\mutation-$($p.Id).log"
    $verdict = ($log | Where-Object { $_ -match '^VERDICT' } | Select-Object -First 1)
    if (-not $verdict) { $verdict = ($log | Where-Object { $_ -match 'VOID|not found' } | Select-Object -First 1) }
    $summary = ($log | Where-Object { $_ -match '^TESTS' } | Select-Object -First 1)
    "{0} | {1} | {2} | {3} | {4}s" -f $p.Id, $p.Desc, $verdict, $summary, [int]((Get-Date) - $t).TotalSeconds | Out-File $progress -Append
}
"rerun finished $(Get-Date -Format s)" | Out-File $progress -Append
