# P18-PERSIST-001 source audit: textual facts about the thermal persistence file that the ownership gate and the tests rely on, printed so that a
# reviewer can read them without running anything. Comments are removed before searching (a comment may name what is forbidden).
param([string]$Out = "source-audit.txt")
$root = "C:\Users\hasib\OneDrive\Desktop\CAD"
$file = "src/io/json/ThermalAnalysisJson.cpp"
function Strip([string]$text) {
    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($text, '//[^\n]*', '')
}
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("P18-PERSIST-001 source audit")
$raw = [IO.File]::ReadAllText("$root\$($file.Replace('/', '\'))")
$code = Strip $raw
$lines.Add("")
$lines.Add("== $file ($((($raw -split "`n").Count)) lines, $($raw.Length) bytes)")
$checks = [ordered]@{
    "unordered containers / std::hash"              = 'unordered_|std::hash'
    "clock, time, random"                           = 'chrono|std::time|clock\(|rand\(|random_device|mt19937'
    "threads / atomics / mutexes"                   = 'std::thread|std::async|std::mutex|std::atomic|std::jthread'
    "const_cast / mutable"                          = 'const_cast|\bmutable\b'
    "mutable globals (static, non-const)"           = '(^|\s)static\s+(?!constexpr|const\b|inline constexpr)[A-Za-z_:<>]+\s+[a-z][A-Za-z_]*\s*(=|;|\{)'
    "text round trip (parse a number)"              = 'stod|stof|stoi|stoll|from_chars|strtod|sscanf|istringstream|stringstream|lexical_cast'
    "mesh identity (NodeId / ElementId / MeshStamp)" = '\bNodeId\b|\bElementId\b|\bMeshStamp\b|\bFacetId\b|facet'
    "derived names (DOF / system / solution / post)" = 'ThermalDof|ThermalSystem|SteadyThermalSolution|TransientThermalSolution|ThermalPostResult|ThermalBalanceResult|ThermalValidationReport|PreparedThermal|ThermalModel'
    "solve / assemble / remesh / validate-run"      = 'solveSteadyThermal|solveTransientThermal|assembleThermalSystem|recoverSteady|recoverTransient|balanceSteady|balanceTransient|\bremesh|generateMesh|validateThermalAnalysis'
    "material properties (P15 owns them)"           = '[Cc]onductivity|[Dd]ensity|[Ss]pecificHeat|specific_heat|[Mm]aterial'
    "resolution / lookup"                           = '\bDocument\b|findObject|modifyObject|[Rr]esolve|regenerat|Regenerator|Mesher'
    "unit conversion / number formatting"           = '273\.15|[Cc]elsius|1000\.0|1e3|\.in\(|to_string|printf|std::fixed|setprecision'
    "revision / timestamp / status flag"            = '[Rr]evision|[Tt]imestamp|[Ss]tale|isCurrent|bool\s+(is)?(valid|current|solved|validated)'
    "raw owning memory"                             = '\bnew\b|\bdelete\b|malloc|free\('
    "goto / setjmp / exceptions thrown"             = '\bgoto\b|setjmp|\bthrow\b'
}
foreach ($name in $checks.Keys) {
    $lines.Add(("  {0,-48} {1}" -f $name, ([regex]::Matches($code, $checks[$name])).Count))
}
$lines.Add("")
$lines.Add("== owners used (each at least as many times as there are places that need it)")
$owners = [ordered]@{ "faceNameToJson" = 2; "faceNameFromJson" = 1; "thermal::validate" = 5; "ThermalAnalysis::create" = 1; "requireObject" = 5; "readNumber" = 8; "readId" = 3; "readEnum" = 4 }
foreach ($n in $owners.Keys) {
    $c = ([regex]::Matches($code, [regex]::Escape($n))).Count
    $lines.Add(("  {0,-32} {1,3}   (at least {2})  {3}" -f $n, $c, $owners[$n], $(if ($c -ge $owners[$n]) { "ok" } else { "TOO FEW" })))
}
$lines.Add("")
$lines.Add("== includes")
foreach ($m in [regex]::Matches($raw, '(?m)^#\s*include\s+[<"][^>"]+[>"]')) { $lines.Add("  " + $m.Value) }
$lines.Add("")
$lines.Add("== the rest of src/io: thermal includes (only the canonical analysis object may appear)")
foreach ($f in (Get-ChildItem -Recurse -File "$root\src\io" -Include *.cpp, *.hpp | Sort-Object FullName)) {
    foreach ($m in [regex]::Matches((Strip ([IO.File]::ReadAllText($f.FullName))), '(?m)^#\s*include\s+[<"]bettercad/thermal/[^>"]+[>"]')) {
        $lines.Add("  " + $f.FullName.Substring($root.Length + 1).Replace('\', '/') + ": " + $m.Value)
    }
}
[IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Get-Content $Out
