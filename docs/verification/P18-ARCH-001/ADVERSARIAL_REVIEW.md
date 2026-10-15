# P18-ARCH-001 — adversarial review

```text
SUBJECT:   an attempt to disprove the thermal architecture before it is built on
QUESTIONS: 34 from the phase plan's final list, read against what exists
           today, plus 6 of the reviewer's own
FINDINGS:  3 -- 1 test weakness closed, 1 zero-match gap closed, 1 recorded
           tag collision
ARCHITECTURE DEFECTS REMAINING: 0
GATE-BLOCKING: 0
```

Each answer below was obtained by reading the code or running something.
Where the answer was "yes, it could", the fix came before the box was ticked.
Where a question is about a type that does not exist yet, the answer says
which milestone owns it and what THIS milestone fixes about it -- which is
the honest answer at an architecture gate, and the one P17-ARCH-001 gave.

## Findings

### F1 — the accepting fixture's transient completeness was asserted nowhere (TEST WEAKNESS, closed)

`ThermalInput_AcceptsAMaterialThatHasOnlyAConductivity` proves that a
material with only `k` is accepted and shows its transient properties absent.
Its contrast, `ThermalInput_AcceptsACurrentMeshWithAUsableMaterial`, assigned
`rho` and `cp` too but never asked P15 whether that material could carry a
transient -- so if a future P15 change made `requireTransientConductionProperties`
refuse it, the "fully characterised" fixture would silently be the same as
the conductivity-only one and the contrast would be a coincidence. Closed by
asserting, in the accepting case, that the transient properties resolve and
equal what was assigned. The pre-freeze run that had just started was
stopped, the test rebuilt, and the run restarted from the beginning.

### F2 — the `[thermal]` Catch2 tag is shared with P15 (RECORDED)

`bettercad_tests "[thermal]"` runs 16 cases, not 11: five P15 cases
(`EngineeringQuantity_Thermal*`, `P15Qual_StoredThermalProperties...`) carry
the same tag. Harmless -- the five are quantity-relation checks that belong
beside a thermal solver -- and the zero-match guard keys on the ctest NAME
prefix `unit\.ThermalInput`, which is exactly 11. Recorded so that a later
milestone does not read "16 thermal cases" as its own count.

### F3 — the zero-match guard did not cover the cycle fixture (GAP, closed)

The first draft guarded `architecture\.checker\.thermal` at a minimum of 3,
which matches `thermal-layer-violation`, `thermal-to-structural` and
`thermal-backend-leak` -- and NOT `meshing-to-thermal`, whose name begins
with `meshing`. The cycle fixture is the one that protects P16, so a filter
that did not count it was the wrong filter. Widened to
`architecture\.checker\.[a-z-]*thermal` at a minimum of 4; `ctest -N`
discovers 4.

## The phase plan's questions, at this gate

**Can P18 solve a stale mesh?** Not through this boundary:
`requireThermalModel` asks `Mesher::currency` against the document as it is
NOW and refuses `StaleGeometry`, `StaleIntent` and `GenerationFailed`
(`ThermalInput_RefusesAMeshOfGeometryThatHasSinceChanged`, `..._RefusesAMeshWhoseSizingIntentHasChanged`,
`..._RefusesAfterAFailedGenerationOfEligibleGeometry`). No function in the
module takes a `VolumeMesh`. What a later milestone must still prove is that
the ONE solve entry goes through this boundary and nothing else does -- P17
proved that with `callers of solveStructuralAnalysis == 1`, and P18-VALID-001
inherits the obligation.

**Can P18 solve geometry that failed regeneration?** Not through this
boundary: geometry eligibility is asked FIRST, through P16's
`geometryIneligibility`, which refuses failed, blocked, stale, bodiless and
configuration-overridden geometry
(`ThermalInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh`). The
failed-rebuild SEQUENCE is P18-QUAL-001's gate, as it was P17-QUAL-001's.

**Can a conductivity edit leave an old result current? density/cp edits? a
thermal-load edit? a thermal-BC edit? a time-step change?** There is no
result type yet. What this milestone fixes is the dependency list a result
must carry -- geometry revision, mesh stamp, material id and revision,
analysis id and revision -- with time settings inside the analysis revision
(`AUTHORITY_MODEL.md`), and the rule that a `rho`/`cp` edit stales a steady
result too, conservatively, recorded so P18-DATA-001 chooses it knowingly.
The `k` half is already answered by P15: `setMaterialThermal` moves the
material's revision, and P17 proved a material-revision move stales a result
through the same `StructuralResultSource` shape P18-DATA-001 will mirror.

**Can NodeId become permanent thermal-load identity? ElementId permanent
thermal intent?** No type in this milestone holds either. `ThermalModel`
borrows the mesh and exposes the `GeometryMeshMap` precisely so that a load
or a BC names a CAD face and resolves to facets NOW (ADR-032, ADR-051). The
compile-fail cases that pin it belong to P18-DATA-001, as `StructuralIdMisuse`
belonged to P17-DATA-001.

**Can a thermal boundary silently move to another CAD face? an unresolved BC
bind to a nearby face?** No resolution code exists yet; the rule is inherited
from P16's mapping contract ("the reference is KEPT and reported; nothing is
rebound") and will be proved by P18-LOAD-001 and P18-BC-001 with the same
no-nearest-face tests P17 wrote. Nothing in `src/thermal/` mentions nearest,
closest or rebind.

**Can an inverted or zero-volume Tet enter thermal assembly?** Two gates
upstream of P18 already refuse it: P16 refuses to HOLD a mesh with a
non-positive element, and this boundary takes only a HELD, CURRENT mesh.
P18-ELEM-001 must still refuse on its own account rather than rely on an
upstream guarantee it does not verify -- ADR-049 says so -- exactly as
Tet4Element does.

**Can thermal conductivity units be confused?** `ThermalModel::conductivity()`
returns a `ThermalConductivity`, a dimensioned `Quantity` (W/(m K)); there is
no `double k`. P15's compile-fail cases already refuse a conductivity used as
a specific heat and a modulus used as a conductivity. The four derived
quantities P18 needs (W/m^2, W/(m^2 K), W/m^3, K/m) do not exist yet and must
arrive as dimensioned aliases with their own compile-fail cases
(P18-DATA-001, P18-LOAD-001; `DEPENDENCY_AUDIT.md`).

**Can K lose symmetry? duplicate contributions overwrite? a free null mode
appear solved? the solver return success with a large residual? heat balance
fail while the solve is accepted? flux or generation fail to preserve the
total? convection use the wrong sign? a rigid transform change T or |q|?**
No matrix, load, BC, solver or balance exists. Each is named to its milestone
in `SCOPE.md`, and the layer decision constrains how the solver questions are
answered: P17's gates are reusable by extraction or re-implemented behind an
independent link, never hand-copied (ADR-050).

**Can GUI visualisation recalculate heat flux? visualisation units mutate
model coordinates? undo restore thermal matrices? save/load trust old
temperature arrays? the CLI implement separate Tet4 thermal logic? the CLI
return zero on a singular solve? thresholds differ between GUI and CLI?
save-before-solve and save-after-solve differ?** Nothing downstream exists.
What this milestone fixes is the authority matrix those milestones are
measured against (`AUTHORITY_MODEL.md`) and the layer that makes the
directions enforceable: `io` and `renderer` may include `thermal`; `thermal`
may include neither; the library may not reach `apps/` (rule 6).

**Can stale binaries produce qualification evidence? a zero-test filter pass?
source change after final qualification?** Answered by the harness, unchanged
from P17: clean builds per preset, `$<TARGET_FILE:>` in every process test,
`ZeroMatchGuard` with asserted minimums (two new filters here, one of them
corrected by F3), and the eight-path fingerprint at both ends of the run.

## The reviewer's own six

**1. Could the thermal boundary and the structural boundary drift apart?**
They could, and that is the cost ADR-051 accepted knowingly in preferring a
mirror to a shared type. The mitigation is that the eleven thermal cases
mirror the ten structural ones case for case, so a change to P16's contract
that breaks one breaks both visibly; and the audit's extraction option is
recorded for a third consumer.

**2. Is `ThermalModel` copyable?** No: copy constructor and assignment deleted,
move defaulted, as `StructuralModel` after P17-ARCH-001's F1. The residual
hazard -- holding one across an edit -- is the same, and the answer is the
same: re-prepare, do not hold.

**3. Does every `InputProblem` have a case that reaches it?** Yes, seven of
seven, and the roster case records it. No value was deleted at this gate
because none was drafted that nothing could return; the enum was written
against P17's already-audited list.

**4. Can a thermal file include `<bettercad/structural/...>` and compile?**
It compiles -- the compiler knows nothing about layers -- and the
architecture test fails the build-level gate: `architecture.checker.thermal-to-structural`
asserts the message naming it, and `architecture.layering` on the real tree
would report the violation. That is the gate, and it ran: 497 files, 0
violations.

**5. Is the layer choice reversible?** Cheaply, now; expensively, later. If
the roadmap's coupling were abandoned, moving `thermal` to 50 is one table
line and one fixture rename today. After a thermo-mechanical milestone has
written `structural -> thermal`, it is a renumber of a qualified module.
ADR-050 is explicit that the number is chosen for the stated coupling and
records the option it closes.

**6. Does `requireThermalModel` do any work the structural boundary does not,
or skip any it does?** Neither. The seven checks are the same seven in the
same order, read side by side (`ThermalAnalysis.cpp` against
`StructuralAnalysis.cpp`); the only difference is the material half --
`requireThermalConductivity` in place of `requireLinearElasticConstants` --
and the test that fixes the difference is the conductivity-only acceptance.

## Verdict

```text
adversarial review     PASS -- 0 architecture defects, 3 findings closed or
                       recorded before the freeze, every forward question
                       named to the milestone that owns it
```
