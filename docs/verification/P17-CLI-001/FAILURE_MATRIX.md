# P17-CLI-001 — the failure matrix

```text
Every row is a REAL document state, driven through the CLI. None is a
hand-assembled diagnostic object, which the brief's §166 forbids.
```

## The matrix

| Fixture | Core result | CLI diagnostic | stdout | Exit | Solver invoked? | PASS |
| --- | --- | --- | --- | --- | --- | --- |
| a valid analysis | accepted, solved | — | payload | 0 | yes | ✓ |
| **a missing material** | the material stage refuses | the core's material code | **empty** | 1 | **no** | ✓ |
| **an under-constrained analysis** | `InsufficientRestraint` | `insufficient_restraint` | **empty** | 1 | **no** | ✓ |
| **an unresolved LOAD target** | `LoadsUnresolvable` | `loads_unresolvable` | **empty** | 1 | **no** | ✓ |
| **an unresolved RESTRAINT target** | `RestraintsUnresolvable` | `restraints_unresolvable` | **empty** | 1 | **no** | ✓ |
| the same, through `structural-validate` | a `rejected` report | the finding's own code | **payload** | 1 | no | ✓ |
| no structural analysis | n/a | `no_structural_analysis` | empty | 1 | no | ✓ |
| two structural analyses | n/a | `ambiguous_structural_analysis` | empty | 1 | no | ✓ |
| no meshing control | n/a | `no_mesh_control` | empty | 1 | no | ✓ |
| the file does not exist | n/a | `document_not_loaded` | empty | 1 | no | ✓ |
| a feature the document lacks | n/a | `bad_face_reference` | empty | 1 | no | ✓ |
| a duplicate `LoadId` | the command refuses | the command's own message | empty | 1 | no | ✓ |
| a `LoadId` that is not there | the command refuses | the command's own message | empty | 1 | no | ✓ |
| a duplicate `RestraintId` | the command refuses | the command's own message | empty | 1 | no | ✓ |
| a `RestraintId` that is not there | the command refuses | the command's own message | empty | 1 | no | ✓ |
| a second analysis | n/a | `structural_analysis_exists` | empty | 1 | no | ✓ |
| fifteen command-line faults | n/a | usage, no code by design | empty | **2** | no | ✓ |

**"Solver invoked? no" is structural, not timed.** The shared `solve()` helper
calls `validateStructuralAnalysisForSolve` and returns before
`solveStructuralAnalysis` when the report is not acceptable — so for every
refused row the factorisation is never reached. That is the brief's §144 and
§145, and probe **L3**, which skips that pre-check, is killed.

Each of the four refusal rows marked in bold is additionally run through **all
four result commands** — solve, displacement, stress, reactions — and each must
fail identically, because they share one helper. The workflow script repeats
three of them against the real executable.

## Two things the matrix is careful about

**The offending entity is not dropped.** An unresolved load still appears in
`structural-settings` after the solve refused it — asserted, because silently
removing the thing that failed would be worse than the failure (§31).

**No CLI default is substituted.** The missing-material row checks there is no
`--default-E` and that the refusal comes from the material stage (§155); the
under-constrained row checks there is no `--auto-pin` and that the diagnostic
is the core's own, including its "six rigid-body modes" explanation (§153).

## Stale geometry and stale mesh: not reachable, and why

The brief's §27 and §28 ask for a stale-geometry and a stale-mesh failure
driven through the CLI. **Neither state can be presented to this CLI**, and
§167 anticipates exactly that — "do not claim impossible persistence
behavior".

The reason is the architecture, not an omission:

```text
every CLI process        loads the document
                         REGENERATES the geometry (and refuses if that fails)
                         GENERATES the mesh from current intent
                         then validates and solves
```

So geometry is current by the time anything asks, and the mesh was built in the
same process from that geometry. There is no window in which a stale mesh could
be handed to the solve, because nothing persists a mesh — P16-PERSIST-001 — or
a result — ADR-048.

What this means for the gate, stated precisely:

- **The core's stale-mesh and stale-geometry refusals remain qualified** by
  P17-VALID-001, which reaches them with an in-process fixture that holds a
  mesh across a geometry edit. Nothing here weakens that.
- **The CLI uses that same gate**, which is what the equivalence tests
  establish: one `solve()` helper, one call to
  `validateStructuralAnalysisForSolve`, no second opinion. If a stale state
  could arise it would be refused, and probe L3 shows the pre-check is
  load-bearing.
- **What the CLI presents instead** is the stale-geometry analogue at its own
  layer: a model that will not regenerate. That path carries three distinct
  codes — `regeneration_failed`, `regeneration_blocked`, `dependency_cycle` —
  which exist because P16-CLI-001's mutation M6 found that checking the
  regeneration *Result* alone let a broken model through. They are reached by
  the `cli.mesh.*` fixtures that share the helper's shape, not by a P17
  fixture, and [EXIT_CODES.md](EXIT_CODES.md) marks those rows as such rather
  than claiming a test this milestone does not have.

Faking a stale-mesh fixture would have meant constructing a
`StructuralValidationReport` by hand and asserting the CLI printed it — which
tests the formatter, not the gate, and is the kind of evidence this project
treats as worse than none.

## Load atomicity

Every refused mutation is checked for **byte equality of the file**, not just
for its exit code:

```text
15 refusal cases x { exit code, stdout empty, stderr non-empty, FILE UNCHANGED }
```

That is `runEdit`'s structural guarantee — it saves only if the edit succeeded —
and the test verifies the consequence rather than trusting the contract. The
`batch` half is checked too: a two-line script whose second line fails leaves
the file exactly as the first line found it.
