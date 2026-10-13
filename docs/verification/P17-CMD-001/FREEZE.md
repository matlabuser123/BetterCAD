# P17-CMD-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: e795acbdf87d65957f71830c3acc7d512da5ceca  (the parent commit)
RAN:       Tue 13/10/2026 16:45:09 -> 20:06:17  =  3h21m07s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **3883 / 3883** | **3883 / 3883** | **3883 / 3883** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| build | 26m21s | 31m56s | 26m53s |
| ctest | 26m37s | 26m07s | 28m23s |
| no-op rebuild | exit 0 | exit 0 | exit 0 |

```text
repeat release-ext   1012 tests x 5   16m19s   exit 0
repeat debug-ext     1012 tests x 5   17m49s   exit 0
```

Every count **measured, not predicted**, and each reconciles against the
element-contour run plus what this milestone added:

```text
suite    3835 -> 3883   +48:  32 StructuralCommand_ cases
                              16 compile_fail.structcmd cases
repeat    980 -> 1012   +32:  the filter is unchanged -- `unit\.Structural`
                              already covers unit.StructuralCommand as a
                              substring. compile_fail is deliberately NOT in
                              the repeat set: each case is a real compilation,
                              so repeating one measures the compiler's
                              determinism and not BetterCAD's.
```

## The tree that was qualified is the tree that was committed

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b
src                a294c27c34dbfabbed4e36013071d07387611e12
tests              1cff71c0b2f77ea388f71bcb02967e0912be293c
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, and `freeze-fingerprint.txt` beside this file carries the
same eight values taken independently, in Bash, before the launch.

Three of the eight moved against `e795acb` and five did not — `include`, `src`
and `tests` changed; `apps`, `examples`, `cmake`, `CMakeLists.txt` and
`CMakePresets.json` are byte-identical. The commands touched no application
code, no reference model and no build configuration.

### The verdict checks were pre-validated, not just run

The element-contour run's verdict printed a false `*** THE TREE MOVED ***`
because `grep -A9 | tail -8` drops the `apps` line and picks up the following
unrelated one. Both checks here extract the block **by name** with `awk`, and
both were validated against a known-good run before being trusted. They
reported correctly, with no false alarm.

## The pre-freeze gates

Cheap first, and **gated on the probes**: a surviving probe stopped the chain
before any of this ran.

```text
0  THE PROBE GATE. C3 survived its first real run, so the chain HALTED here
   rather than spending 45 minutes of checks and three hours of qualification
   on a known test gap. It resumed only once C3 was killed.
1  both mutated sources byte-compared against their pristine copies
2  line endings: no mixed file in the changed set
3  git diff --check -- no whitespace errors
4  no BETTERCAD_*_EXPORT on a header-defined constexpr or inline, across the
   five structural headers this milestone touched
5  the FULL default target built -- the clean stage deletes the application,
   so a targeted rebuild would leave gui.* with no executable
6  counts recounted with ctest -N
7  the 32 command cases under --repeat until-fail:5  ->  32/32
8  the debug-shared-ext build  ->  0 diagnostics, and 357/357 on the
   structural filter in that preset
```

Gate 8 earned its place for the usual reason: `SolverSettings` moved between
headers, and an export defect across the DLL boundary fails **only** in that
preset.

## Four predecessors requalified, and the debt is named

```text
P17-DATA-001    StructuralAnalysisDefinition gained `SolverSettings solver`
P17-SOLVE-001   SolverAlgorithm, SolverSettings and validate(SolverSettings)
                moved to StructuralData.hpp
P17-VALID-001   solveStructuralAnalysis reads the settings from the DOCUMENT
                and lost its parameter
P17-LOAD-001    validate(const StructuralLoad&) exposed, sharing the
                isFiniteValue helper the prepare path already used
```

None of these is incidental: the settings had to become canonical for a
command to edit them, and a command had to be able to refuse a NaN force
without a mesh in hand.

## The harness

Byte-identical to the one P16-SIZE-001 settled on, **twenty-seven milestones in
a row** counting this one. Both hash forms recorded, because they differ and a
hash without its algorithm cannot be checked:

```text
qualify.cmd          git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
                     sha1sum         244b38de8feac0475af7cdc56ba9415d27d8dd29
verify-harness.cmd   git hash-object 11855b8ff891093b021d1e0351761283d0eedd24
                     sha1sum         afd0197d4f4726db7f533c2f27f4f6e069ba83c6
```

`run-qualification.cmd`'s every-line-begins-with-`rem`/`set`/`call`/`echo`
invariant was checked before the run; no stray line was found.
