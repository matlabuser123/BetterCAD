$s = "C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad"; $root="C:\Users\hasib\OneDrive\Desktop\CAD"
$cpp="$root\src\cfd\CfdAnalysis.cpp"; $chk="$root\tests\architecture\CheckLayering.cmake"; $tgt="$root\tests\architecture\CheckTargetDependencies.cmake"
$list=@(@("CP01",$cpp,"geometry ineligibility reported as no mesh"),@("CP02",$cpp,"mesh currency is not asked"),@("CP03",$cpp,"the density is not required"),@("CP04",$cpp,"a stale mesh is reported as no mesh"),@("CP05",$cpp,"no geometry revision carried"),@("CP06",$cpp,"no material id carried"),@("CP07",$cpp,"missing material reported as a bad density"),@("CP08",$chk,"cfd at layer 60"),@("CP09",$chk,"cfd at layer 47"),@("CP10",$tgt,"cfd -> thermal link not refused"))
foreach ($p in $list) {
  $before=[IO.File]::ReadAllText($p[1])
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$s\mutate-cfd.ps1" -Name $p[0] -File $p[1] -FindFile "$s\find-$($p[0]).txt" -ReplaceFile "$s\replace-$($p[0]).txt" | Out-Null
  $log=Get-Content "$s\mutation-cfd-$($p[0]).log" -Raw
  $verdict = if ($log -match 'VERDICT: (\w+[^\r\n]*)') { $Matches[1] } else { 'UNKNOWN' }
  $tests = if ($log -match 'TESTS : ([^\r\n]*)') { $Matches[1] } else { '' }
  $cases = if ($log -match 'FAILED CASES : ([^\r\n]*)') { $Matches[1] } else { '' }
  $after=[IO.File]::ReadAllText($p[1]); $r= if ($before -ceq $after) {'restored'} else {'NOT RESTORED'}
  Add-Content "$s\cfd-mutation-progress.txt" ("{0} | {1} | {2} | {3} | {4} | {5}" -f $p[0],$p[2],$verdict,$tests,$cases,$r) -Encoding utf8
}
Add-Content "$s\cfd-mutation-progress.txt" "campaign finished"