# P18-POST-001 — who computes the fields (GUI ownership)

```text
SUBJECT:  the recovered fields are computed once, in the thermal module; a viewer, report, command or exporter consumes them
```

## The rule

`grad(T) = G Te` and `q = -k grad(T)` are computed in ONE place: `src/thermal` + `include/bettercad/thermal`. A second computation is a second
authority that drifts from the first (a different sign, a different element order, a smoothed variant), so no file outside the thermal module may rebuild
the arrays from the mesh and the nodal temperatures. A viewer asks for a `ThermalPostResult` and reads it.

## The gate: `tests/architecture/CheckRecoveryOwnership.cmake`

Run as `cmake -DSOURCE_DIR=<root> -P CheckRecoveryOwnership.cmake`, over the source text with `//` comments removed (the repository documents with
`//` and `///`).

| Rule | Checks | Fails with |
| --- | --- | --- |
| 1 | no file under `apps/`, `src/`, `include/`, `examples/` **outside** `src/thermal` and `include/bettercad/thermal` names `computeTet4Geometry`, `gradientFrom` or `recoverThermalElementFields` | "GUI or other non-thermal code recomputes the recovered fields (<name>); consume the core result instead." |
| 2 | `ThermalPost.cpp` / `ThermalPost.hpp` contain no `Jacobian`, `adjugate`, `cofactor`, `shapeGradient`; and `ThermalPost.cpp` contains `gradientFrom` | "rewrites Tet4 geometry (<name>); P18-ELEM-001 owns G." / "does not reach G through gradientFrom." |

Callers of the module's source-bound entry points (`recoverSteadyThermal`, `recoverTransientThermal`) are not flagged: asking the core for a result is not
recomputing it. (`recoverThermalElementFields` is public deliberately, as the algebraic seam the tests drive; the gate is what stops a viewer from using
it as a shortcut.)

## Evidence

Output of the gate (`ownership-evidence.txt` in `qualification/`):

```text
real tree                 Recovery ownership check passed: 497 files outside the thermal module scanned, 0 violations
fixture recovery-valid            passed: 3 files outside the thermal module scanned, 0 violations                 exit 0
fixture recovery-gui-recomputes   FAILED: 1 violation: apps/app/Viewer.cpp: ... recomputes the recovered fields (gradientFrom)   exit 1
fixture recovery-rewrites-geometry FAILED: 1 violation: src/thermal/ThermalPost.cpp: rewrites Tet4 geometry (Jacobian)             exit 1
```

Four ctest entries run it: `architecture.recovery-ownership` (the real tree), and `architecture.recovery-ownership.valid`, `.gui-recomputes` and
`.rewrites-geometry` (the three fixtures, asserting exit code and message). A gate that always passed would be vacuous; the two failing fixtures are what
prove it can fail, and the valid fixture proves a legitimate consumer is not flagged.

## Formula ownership

| Formula | Owner | Other places it appears (audited) |
| --- | --- | --- |
| Tet4 geometry, `G`, `grad(T) = G Te` | P18-ELEM-001 | none: `ThermalPost.cpp` calls `gradientFrom` once |
| gather of `Te` through the numbering | P18-DOF-001 `elementDofs` | none |
| `q = -k grad(T)`, `|q|` | P18-POST-001 `fieldsFrom` | none outside `ThermalPost.cpp`; the tests carry their own independent oracle |
| `k` | P15 through P18-MAT-001 | read at each recovery, not cached |
| colour, range, legend, component choice | P18-VIZ-001 (not started) | view state, never in the result's source |
| heat balance, reactions, boundary flux | P18-BALANCE-001 (not started) | absent (compile-fail `THE_RESULT_HAS_NO_HEAT_BALANCE`, `…NO_REACTIONS`, `…NO_BOUNDARY_FLUX`) |

## Existing GUI

No application code in this tree uses a thermal result yet, so the real-tree scan has nothing to flag; the gate is a standing constraint on the milestones that will
(P18-VIZ-001, P18-CMD-001). They receive `ThermalPostResult` and cannot add a recomputation without turning four tests red.
