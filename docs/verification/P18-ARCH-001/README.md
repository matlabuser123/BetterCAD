# P18-ARCH-001 — Thermal Architecture / Scope

```text
STATUS:    PASS
MILESTONE: P18-ARCH-001, the first milestone of P18 — Thermal Analysis
SCOPE:     architecture only. No element, no DOF, no conductivity or capacity
           matrix, no load, no boundary condition, no time step, no solve.
```

## Baseline

```text
HEAD at start      a3b721b150338cc1cbd24ee592a11a70a6196c0c  (P17-QUAL-001's
                   documentation record, on top of ae8c2f7)
origin/main        a3b721b150338cc1cbd24ee592a11a70a6196c0c
working tree       clean but for the untracked .claude/ settings directory
P17 source trees   apps a95876bc, examples a955cf65, cmake 5a382115,
                   CMakeLists.txt 3ec3c10f, CMakePresets.json 3229f0f8 are
                   P17-QUAL-001's values on THIS tree, so the predecessor is
                   demonstrably qualified here and not merely recorded as such
authorized         2026-10-16, "start P18", with the phase plan now in TODO.md
```

## Prerequisites

```text
P15 — Materials / Engineering Data    QUALIFIED
P16 — Meshing                         QUALIFIED, 15 of 15, 3387/3387 x 3
P17 — Structural FEA                  QUALIFIED, 17 of 17, 4009/4009 x 3,
                                      findings F1-F3 carried unchanged
```

## What this milestone found before it built anything

**The audit changed the work, as P17's did.** P15 built this phase's
material boundary *for this consumer, by name*, and P17 built, qualified and
audited the device that closes P16's caller gap:

```text
features::requireThermalConductivity    "What steady-state conduction needs
                                        from a material ... never returns a
                                        default conductivity" -- refuses a
                                        missing, non-finite or non-positive k
features::requireTransientConductionProperties
                                        rho, cp and k, every missing input
                                        named at once
ConsumerKind::ThermalSteady,            P15's completeness split, already
  ThermalTransient                      by consumer
ADR-027's law contract                  the shape a temperature-dependent k
                                        will take, fixed before P18 existed
ADR-036 / StructuralModel               possession is the proof: the device,
                                        the check order, the enum-honesty rule,
                                        verified through sixteen milestones
```

What was left to build is the thermal instance of that device, in a module
placed where the roadmap's coupling can reach it, and the three decisions
that fix the phase's scope, layer and boundary. Full audit:
[DEPENDENCY_AUDIT.md](DEPENDENCY_AUDIT.md).

## The solver scope

```text
heat conduction | constant isotropic k | Tet4 | one solid | one scalar T per node
steady  K T = Q        transient  C dT/dt + K T = Q, backward Euler
loads   q'' on a face, q''' in the body
BCs     prescribed T, convection -k grad(T).n = h (T - T_inf)
```

Twenty unsupported behaviours are listed and **refused rather than
approximated** -- radiation, phase change, contact, anisotropy, property
laws, coupling, a second solid among them -- because a best-effort
temperature is worse than a refusal: the user cannot see the difference.
Transient is IN scope because P15 already split the consumer and the
implicit step reuses the steady system; thermo-mechanical coupling is OUT
because it changes P17's formulation and is a separate scope decision.
[SCOPE.md](SCOPE.md), [ADR-049](../../architecture/decisions/ADR-049-the-first-thermal-solver-is-linear-conduction-on-tet4-steady-and-transient-on-one-solid.md).

## The module and its layer

```text
core 0   sketch 10   features 20   assembly 30   drawing 40   meshing 40
thermal 45   structural 50   io 60   renderer 70   scripting 70
```

`thermal` takes **45, below `structural`**. The roadmap's one known coupling
is a temperature field driving thermal expansion and stress -- `structural`
READING a `thermal` result -- and the rule is strictly-lower, so the reader
must be above the read. 45 is the free integer ADR-035 respaced the table to
provide, and no module was renumbered. The mirror image is the cost and is
enforced by a fixture: `thermal` can never include `structural`, so P17's
sparse solver is reused by extraction below 45 or by an independent private
link (P18-SOLVE-001 decides), never by include and never by a hand copy.
[LAYERING.md](LAYERING.md), [ADR-050](../../architecture/decisions/ADR-050-a-thermal-module-at-layer-45-below-structural-so-the-coupling-can-flow-down.md).

## The input boundary

```cpp
Result<ThermalModel> requireThermalModel(
    const Document&, const features::Regenerator&,
    const meshing::Mesher&, MeshControlId);
```

**No function in the module takes a `VolumeMesh`.** `ThermalModel` has one
private constructor, exactly one friend, and is move-only. Possession proves
the control exists, the body is eligible (including not behind a
configuration override), a mesh is held and Current, the map and quality
report came from that mesh in one lookup, and the material yields a usable
`k`. It does NOT prove mesh quality, load or BC resolution, a temperature
datum, or transient completeness -- those are STAGED after the boundary,
the shape P17 arrived at (ADR-042), adopted here from the start rather than
forecast and amended. A material with only a conductivity is accepted: what
a transient needs is a per-mode question for P18-MAT-001.
[AUTHORITY_MODEL.md](AUTHORITY_MODEL.md), [ADR-051](../../architecture/decisions/ADR-051-a-thermal-analysis-input-is-validated-once-and-possession-is-the-proof.md).

## Authority and invalidation

Analysis intent is canonical, the solution is not. Three rows worth
repeating from the matrix:

```text
a k edit                         -> result stale, mesh CURRENT
a rho or cp edit                 -> a STEADY result stale too: the material
                                    revision moved and the result does not
                                    know which property; conservative,
                                    recorded for P18-DATA-001 to choose knowingly
a time-step or end-time edit     -> result stale: time settings are intent
```

Diagnostics as enumerators plus a message, SI throughout with one m -> mm
conversion in the renderer adapter, ordering by the mesh's own enumeration
and never a container's. [AUTHORITY_MODEL.md](AUTHORITY_MODEL.md).

## Tests

```text
tests/thermal/ThermalAnalysisBoundaryTests.cpp         11 cases, 280 assertions
                                                       with P15's five [thermal]
                                                       quantity cases
  accepts a current mesh with a usable material        + possession carries the
                                                         mesh, a complete map
                                                         whose stamp matches it,
                                                         the quality report and
                                                         P15's k in SI; and the
                                                         fixture is shown to
                                                         carry a transient
  accepts a material with ONLY a conductivity          the line between this
                                                         boundary and
                                                         P18-MAT-001, with the
                                                         transient inputs shown
                                                         absent
  refuses a mesh of geometry that has since changed    the sharp case: the
                                                         document is REGENERATED
  refuses a mesh whose sizing intent changed
  refuses after a failed generation of ELIGIBLE geometry
  refuses an ineligible body before looking at the mesh  proves check order
  refuses an unknown control / nothing meshed
  refuses no material / a material without k            P15's diagnostic names
                                                         "thermal conductivity"
                                                         and the material
  reaches every InputProblem it declares                7 of 7

tests/architecture/  4 new fixtures, each one violation, each asserting the
                     message that names it
  thermal -> io                 REFUSED
  thermal -> structural         REFUSED   (the direction ADR-050 fixes)
  meshing -> thermal            REFUSED   (the cycle)
  nglib.h in src/thermal/       REFUSED
  valid/src/thermal/            ALLOWED, and it holds the layer-table entry
                                in place
architecture.layering on the real tree:  497 files, 0 violations
```

## Zero-match protection

Checked rather than assumed, with `-N` before the real run:

```text
ctest -R 'unit\.ThermalInput' -N                          Total Tests: 11
ctest -R 'architecture\.checker\.[a-z-]*thermal' -N        Total Tests: 4
ctest -R '^architecture\.' -N                             Total Tests: 19
whole suite                                               Total Tests: 4024
```

Both new filters are in `ZeroMatchGuard.cmake` with asserted minimums; the
second was widened during the review (F3).

## Adversarial review

```text
QUESTIONS                 40  (34 from the phase plan, 6 of the reviewer's)
FINDINGS                   3
ARCHITECTURE DEFECTS       0
TEST WEAKNESS CLOSED       1  the accepting fixture's transient completeness
                              is now asserted, so its contrast case is a
                              contrast and not a coincidence
ZERO-MATCH GAP CLOSED      1  the guard did not count the cycle fixture
RECORDED                   1  the [thermal] tag is shared with five P15 cases
GATE-BLOCKING              0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The permitted downward directions (io -> thermal, renderer -> thermal,
  structural -> thermal) are proved by the layer table and not by a compiled
  include, because nothing consumes the module yet. The FORBIDDEN directions
  are proved by fixtures, which is the half that prevents a mistake.

A ThermalModel borrows the Mesher's mesh, map and quality report. Move-only
  removes the realistic way one escapes; holding one across an edit still
  dangles, and the answer is to re-prepare rather than hold (P17-ARCH-001 F1,
  inherited).

Two boundaries that must stay in step. ADR-051 chose a mirror of P17's
  boundary over a shared type, knowingly; the eleven thermal cases mirror the
  ten structural ones so a P16 contract change breaks both visibly. A third
  consumer is the point at which extraction earns its seam.

The four derived SI quantities P18 needs (W/m^2, W/(m^2 K), W/m^3, K/m) do not
  exist in core yet. P18-DATA-001 and P18-LOAD-001 add them as dimensioned
  aliases with compile-fail cases, the way P15 added its own.

The scope statement cannot be tested; what is verified is that the statement
  and the code agree -- one translation unit in src/thermal/, no element, no
  matrix, no linear algebra linked.

P17's F1, F2 and F3 stand. Element-constant flux on Tet4 inherits F2's
  mechanism and F3's cause; thermal flux-concentration cases must be
  convergence studies.

toString(ThermalConductivity) prints the base SI form, "45 kg*m/(s^3*K)",
  rather than W/(m K) -- visible in the boundary suite's WARN line. A P15
  presentation detail inherited unchanged; a thermal report that wants the
  engineering form owns the formatting, and the value is exact either way.

This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 01:56:17 to 05:26:04,
3h29m47s, 17 stages, `qualify.cmd exit 0`. Before it, the working tree passed
the whole suite in debug-ext (4024/4024, 1641.74 s, `qualification/prefreeze.txt`).

```text
PRESET            CONFIGURE  CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext              0       0   27m21s       1 edge      29m14s   4024/4024
release-ext            0       0   33m09s       1 edge      26m28s   4024/4024
debug-shared-ext       0       0   26m38s       1 edge      28m25s   4024/4024

REPEAT (5x each of 1177 selected tests, back to back)
release-ext            0                            1177/1177   1074.75 s
debug-ext              0                            1177/1177   1209.68 s

warnings, all three clean builds   0   (-Werror, 660 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       11 DLLs, including libbettercad_thermal.dll
```

**4024** is P17-QUAL-001's 4009 plus this milestone's 11 boundary cases and
4 checker fixtures. **660** objects is 658 plus the thermal library's one
translation unit and the new test file. The boundary suite gives the identical
259 assertions in 11 cases under `-O0 -g`, under the optimiser and across a
DLL boundary. [FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    4024/4024 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 660 objects; 1177 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     include 33ea62c2, src 9e992c58, tests 360b9e04 moved; apps,
          examples, cmake, CMakeLists.txt, CMakePresets.json identical to
          the P17-QUAL-001 freeze; identical before and after the run
EVIDENCE: this directory
ADRs:     ADR-049, ADR-050, ADR-051
```

## Revision

First issue, 2026-10-16.
