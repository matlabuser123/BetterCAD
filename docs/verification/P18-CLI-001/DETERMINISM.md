# P18-CLI-001 — determinism

## Claims, and the test that carries each

| Claim | Evidence |
|---|---|
| Two fresh processes running the same file print **byte-identical** `--json` solve payloads | workflow step 10 (steady), the transient pair, `from_elsewhere`, `spaced` (same payload from another working directory and a path with a space) |
| Adding and removing a load restores the payload byte for byte | workflow steps 11–12 |
| A batch builds the same analysis as the edits one at a time | workflow `batched` comparison |
| In-process: the same invocation twice prints the same text | `ThermalCli_InspectIsDeterministic` and every `...Structured` test |
| The extremum tie-break is the **lowest id**, whatever the order the ids arrive in | `ThermalCli_TemperatureExtremaTieBreakIsTheAscendingNodeId`, `ThermalCli_ExtremumTieBreakIsTheLowestIdWhateverTheOrder` (hand-built input with unordered ids and equal values, nodes and elements) |
| The order of keys in a payload is the order of insertion, not a hash order | `JsonValue` keeps insertion order (P17 precedent); `tests/cli/MiniJson.hpp` reports key order and the tests assert it for the headline objects |
| The CLI result equals the core result bit for bit, independent of the CLI's own arithmetic | CORE_EQUIVALENCE.md |

## What the CLI does to stay deterministic

* It computes nothing that could vary: no summation order, no parallel reduction, no hash container is iterated. Every number it prints is a field the core produced; extrema are found by one linear scan with an explicit, documented tie-break on the stored id.
* It sorts nothing by pointer or address.
* It prints no time, no process id, no path, no environment, no host name and no thread count in a payload.
* Numbers are written by the existing `JsonValue` writer, which prints the shortest round-trip form; that is the same function in every process.

## What it does not claim

* Not bit-identical to a *different compiler or optimisation level*. Floating-point results of the core may differ in the last bits between debug and release builds. The three-preset comparison (`postrun-cli.ps1`) therefore requires, across presets, the **same exit code for every one of the 74 processes**, the **same key set in every payload**, the **same text and discrete fields**, and the **same SHA-256 for every canonical document the workflow edits**; numeric fields are compared as numbers and the worst relative difference is *measured and reported*, not assumed to be zero. Bit equality is required within one preset, across processes (the workflow's byte-identical payload checks).

**Measured in the qualification run** (`postrun-cli.ps1`, three presets, 74 processes each): every one of the 74 processes has the **same exit code** in all three presets; the 27 JSON payloads have **identical key sets and identical text fields**, and **every numeric field is bit-identical** between debug-ext and release-ext and between debug-ext and debug-shared-ext (worst relative difference **0**); the 11 canonical documents the workflow edits have the **same SHA-256** in all three presets. The 24 process lines whose stdout bytes differ are all explained: 23 print the path of the document, which contains the preset's build directory (`debug-ext` vs `release-ext` is +2 bytes, vs `debug-shared-ext` +7), and equal the debug-ext line once the build root is normalised; the 24th is `version`, which reports the build type and library kind by design. That bit-identity across optimisation levels is a **measurement of this run on this compiler**, not a guarantee the CLI makes.
* Not independent of the mesher. The mesh is Netgen's; a different Netgen version would change node counts. The workflow asserts relations (a local size changes the node count; generation × mesh volume equals the balance's generated heat to eleven digits), not mesh sizes.
