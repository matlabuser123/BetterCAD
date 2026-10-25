# P18-CLI-001 — structured output

```text
envelope           none:  ONE JSON object per payload, exactly as the sibling reports write it (StructuredOutput.hpp)
field names        lowerCamelCase, naming BetterCAD concepts, never a backend's
order              INSERTION order (a vector, never a map), so a payload is deterministic with no sorting
quantities         {"value": v, "unit": "K"}  — never a bare engineering number;  counts and ids are bare integers
enumerations       the core's own stable strings: "steady", "transient", "surface_heat_flux", "volumetric_heat_source",
                   "prescribed_temperature", "convection", "every_step", "final_only", "PASS", "FAIL", "TV_MESH_CURRENT", "mesh_stale" …
non-finite         never written as a number (see below)
```

## Which stream, and when (the brief's §60–66)

| Situation | stdout | stderr | exit |
| --- | --- | --- | --- |
| success | **exactly one JSON object** (`--json`) or the prose form | empty | 0 |
| a command that cannot answer | **empty** | one line `bettercad-cli <command>: <code>: <message>` | 1 |
| an unreadable command line | **empty** | the problem and the usage line | 2 |
| `thermal-validate` / `thermal-balance` on an analysis that is not accepted | the payload (the report *is* the answer) | empty | 1 |

Failures do not put a structured error object on stdout, **by the project's convention**, followed throughout (P15-CLI-001 onwards): a pipeline that sees empty stdout and a nonzero exit has a complete account, and the stable code is where a script can find it without parsing the sentence after it. Mixing the two conventions per command would be the thing the brief forbids ("do not mix ad hoc").

## The gate

* **In process**, `ThermalCli_StructuredStdoutIsClean` runs all seven reports for a steady and a transient analysis and requires, for each, that the output parse with `MiniJson.hpp` — a strict RFC 8259 reader written for the tests that rejects trailing text, a second value, `nan`/`inf`/`Infinity`, a leading zero, a single-quoted string, an unescaped control character and a duplicate key (the reader is itself tested: `ThermalCli_TheTestParserIsStrict` feeds it seventeen malformed inputs and requires every one to be rejected). It also requires `{` first, `}\n` last, no ANSI escape, and an empty stderr.
* **In separate processes**, `cli.thermal.workflow` reads the real executable's stdout through CMake's `string(JSON)` — a genuine parser used with no `ERROR_VARIABLE`, so any contamination is a fatal error rather than a regex that still happened to match. No `tail`, no `sed`, no `grep '{'`, no `2>/dev/null` is used anywhere (the chatter this protects against is on stdout, so redirecting stderr would not have helped anyway).
* **Netgen's chatter** (a warning written to stdout when a local size restriction is applied) is contained in the Netgen adapter (`SilentBackendScope`), where P16-CLI-001 put it; nothing in the thermal CLI filters it. The workflow solves a document with a local refinement so the path is live. See [STDOUT_CLEANLINESS.md](STDOUT_CLEANLINESS.md).

## Non-finite numbers

`JsonValue::number(double)` writes `std::format("{}", v)`, which is `nan` or `inf` for a non-finite value — not JSON. The earlier reports never met one because a validator had refused it first. A thermal **validation report** can: a finding about a residual that is not finite carries, as its measured value, a number that is not finite. `FiniteJson` (`ThermalOutput.hpp`) writes a number only if it is finite and otherwise writes `null` and remembers it:

* in a **result** report (temperature, gradient, heat flux, balance, solve, settings) the memory becomes a refusal — `nonfinite_value`, exit 1, nothing on stdout;
* in the **validation** report the finding keeps its place, its value is `null` and `"valueFinite": false` says so, so the one payload that explains the failure stays parseable.

The ownership gate counts: `JsonValue::number(` may appear only in the two size helpers and `JsonValue::quantity(` never, so a double cannot bypass the writer (probe CR15 is killed by it; CO01/CO02 by the unit test).

## The payloads

All carry `analysisId`, `name`, `mode`, `nodeCount` and `tetrahedronCount` except `thermal-settings` (canonical intent only: it never meshes). **`tetrahedronCount` is the number of tetrahedra, not the mesh total** (which also counts boundary triangles) — P17-CLI-001 shipped a 24 for a 12-tetrahedron block until its equivalence test compared the printed count with the core's; the lesson is applied here and tested.

```text
thermal-settings    analysisId name mode meshControl resultStored(false)
                    loads[]               loadId type face? faceResolves? flux{W/m^2}? generation{W/m^3}?
                    boundaryConditions[]  boundaryConditionId type face faceResolves temperature{K}? coefficient{W/(m^2 K)}? ambient{K}?
                    initialTemperature{K}|null  solver{relativeResidualTolerance pivotFloor}  transient{timeStep{s} endTime{s} output}|null
thermal-validate    … accepted stoppedAt|null failure|null validation
thermal-solve       … freeEquations constrainedCount temperature  [steady: normalizedResidual pivotRatio]
                    [transient: stepCount stateCount startTime{s} endTime{s} worstPhysicalResidual worstReducedResidual]
                    heatBalance{balanced worstNormalizedResidual} validation
thermal-temperature … state{index time{s}|null} temperature{unit count min{node value unit} max{node value unit}}  [transient: history[]{index time min max}]
thermal-gradient    … domain("element") quantity unit("K/m") count magnitude{min max unit} largest{element magnitude gradient{x y z unit}} smallest{element magnitude}
thermal-heat-flux   … domain quantity("conductive_heat_flux") unit("W/m^2") count magnitude{…} largest{element magnitude flux{x y z unit}} smallest{…}
thermal-balance     … accepted balanced worstNormalizedResidual stateCount final{…} loads[] convections[] reactionCount  [transient: steps[] integrated{…}] validation

validation{ status accepted inputsAccepted solveAccepted errorCount warningCount infoCount policy
            checks[]{rule("TV_…") group status blockedBy?}      all fifteen, in the core's order
            findings[]{code rule severity message subject? metric? element? step? time? stepSize? value? valueFinite? threshold? unit? count? cause?}
            metrics{…} }
```

`thermal-gradient` and `thermal-heat-flux` are **per-tetrahedron constant values** (a linear tetrahedron has a constant gradient) and say so (`domain: "element"`); nothing is smoothed to nodes. The flux is labelled `conductive_heat_flux` and points where heat *flows* (`q = −k grad T`); it is not the applied scalar surface flux of a load, whose positive sign means heat *into* the body — the two have different field names everywhere they appear.

**The summary is bounded**: it holds counts, a range and the element or node carrying each end. The one place a payload grows with a quantity is `thermal-balance` for a transient analysis (one small object per integration step) and `thermal-temperature`'s `history` (one per *stored state*) — both are bounded by the number of steps, never by the number of nodes or elements, and neither omits a step.
