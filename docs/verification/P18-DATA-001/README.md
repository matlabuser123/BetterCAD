# P18-DATA-001 — Thermal Analysis Data Model

```text
STATUS:    PASS
MILESTONE: P18-DATA-001, the second milestone of P18 — Thermal Analysis
SCOPE:     identity, canonical intent, derived result representation,
           provenance, currentness, the state model and the result lifecycle.
           No element, no DOF numbering, no matrix, no assembly, no solve, no
           Fourier post-processing, no convection integration, no GUI.
```

## Baseline

```text
HEAD at start      e6fded36acfa1fec41670ed6b14b7a243c145d4a  (P18-ARCH-001's
                   documentation record, on top of 7e7578f)
HEAD^{tree}        a65a57a89cd5f6fcdc96d75614a49adebb559b3f
origin/main        a3b721b150338cc1cbd24ee592a11a70a6196c0c  -- TWO COMMITS
                   BEHIND: the P18-ARCH-001 push had not reached the remote
                   when this milestone started, and is verified with this
                   milestone's push
working tree       clean but for the untracked .claude/ settings directory;
                   git diff --check clean; nothing staged
P18-ARCH-001       PASS, 19 of 19, 4024/4024 x 3, and its eight-path
                   fingerprint equals commit 7e7578f's subtrees
                   (apps a95876bc, include 33ea62c2, src 9e992c58, tests
                   360b9e04, examples a955cf65, cmake 5a382115,
                   CMakeLists.txt 3ec3c10f, CMakePresets.json 3229f0f8)
P15 / P16 / P17    QUALIFIED
authorized         2026-10-16, "start", with the owner's P18-DATA-001 brief
```

## Prerequisites

```text
P18-ARCH-001  the thermal module at layer 45, the scope (ADR-049), the input
              boundary requireThermalModel (ADR-051)      PASS
P17           Structural FEA                               QUALIFIED
P16           Meshing                                      QUALIFIED
P15           Materials / Engineering Data                 QUALIFIED
```

## What the audit found before anything was built

The brief required the existing data architecture to be audited first, and
the audit decided the shape of everything that followed: **every mechanism
this milestone needs already existed, and P18 adds types, not machinery.**

```text
strong IDs            core/Id.hpp's Id<Tag> and isDocumentObjectTag  -> 3 tags
source fingerprint    P17's revision tuple, not a hash               -> mirrored, + mode
currentness           one place decides, by source equality         -> mirrored
document object       DocumentObject + modifyObject + revision       -> ThermalAnalysis
input gates           requireThermalModel (ADR-051)                  -> called, not re-asked
material revision     requireEffectiveMaterial + Material::revision -> read into the source
result container      immutable, validating factory, dense arrays   -> ThermalResult
face targets          FaceName + core's validate(FaceSelector)      -> the only target
command history       stores commands, clones objects               -> unchanged, pinned by test
persistence           refuses an unknown object type by name        -> unchanged, pinned by test
units                 Quantity<Dimension>, Unit, literals, catalog  -> 4 of each
```

Full table, with the reason for each decision: [DATA_MODEL.md](DATA_MODEL.md)
and [IDENTITIES.md](IDENTITIES.md).

## The identity model

```text
DOCUMENT      ThermalAnalysisId (widens to ObjectId), ThermalLoadId,
              ThermalBoundaryConditionId (members; do not widen). Canonical,
              persisted, survive a remesh. In core/Id.hpp with every other
              document identity. Distinct from P17's AnalysisId, LoadId and
              RestraintId: nothing converts between the two phases.
CAD INTENT    FaceName. The only target a load or boundary condition has.
MESH-LOCAL    NodeId, ElementId, MeshStamp. P16's, unchanged. Nothing
              canonical holds one.
SOLVER-LOCAL  not defined yet (P18-DOF-001).
```

24 compile-fail cases refuse every conversion that would break it, and the
one widening the model permits is exercised by the control.
[IDENTITIES.md](IDENTITIES.md).

## Canonical and derived

| Canonical (document state) | Derived (never canonical) |
| --- | --- |
| `ThermalAnalysisDefinition`: mesh control, mode, loads, boundary conditions, initial condition, solver settings, transient settings | DOF map, K, C, Q, T, grad(T), q, heat balance, sample time, provenance, currency, state, visualisation buffers |

The line is enforced three ways: the result header is not reachable from the
definition's header; a size mirror breaks the build if a field is added to
the definition; `static_assert(!is_constructible_v<Definition, Result>)` and
six compile-fail cases spell the forbidden members out. **Strict
mode-completeness**: a steady definition may not carry an initial condition
or time settings, a transient one must carry both.
[CANONICAL_DERIVED.md](CANONICAL_DERIVED.md).

## Units

Four quantities added to core, composed so the composition is the proof:
`TemperatureGradient` K/m, `HeatFlux` W/m^2, `HeatTransferCoefficient`
W/(m^2 K), `VolumetricHeatSource` W/m^3; literals, units and catalog entries
(54 -> 58). Fourier's law, convection and the balance are dimensionally
checked at compile time. **Absolute temperature only**: one function,
`isAbsoluteTemperature`, refuses NaN, infinity and anything below 0 K at
every temperature that enters the model, including a result's nodes; 0 K is
accepted and the reason is written down. **No Celsius exists**, proved as a
fact about the whole unit catalog. Heat flux points where heat flows; heat
into the body is positive everywhere. [UNITS.md](UNITS.md).

## Provenance and currentness

```cpp
struct ThermalResultSource {
    ObjectId body; MeshControlId control;
    meshing::GeometryRevision geometry; meshing::MeshStamp mesh;
    MaterialId material;  std::uint64_t materialRevision;
    ThermalAnalysisId analysis;  std::uint64_t analysisRevision;
    ThermalAnalysisMode mode;
};
```

A revision tuple, not a hash: trivially copyable, `sizeof` asserted equal to
its fields, no pointer, no clock, no iteration order. **Currentness compares
source identity, never arrays and never sizes.** The two rows the owner asked
for by name:

```text
same NodeIds, same counts, new mesh generation   STALE, reason mesh. Measured:
                                                 9 nodes with identical IDs and
                                                 12 tetrahedra either way, stamps
                                                 differ, describes() false,
                                                 temperatureOf() refused
same values, new source generation               old STALE, new CURRENT -- and
                                                 the arrays are asserted equal
                                                 first, so nothing in them could
                                                 have told the two apart
```

Every definition field is edited in the real document, one at a time, and
stales the result while the mesh stays current; a conductivity edit 45 -> 50
W/(m K) stales the result for reason `material` and not the mesh.
[SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md).

## State and lifecycle

Six derived states -- `NoAnalysis`, `InputsUnavailable` (the brief's
"defined"), `Ready`, `SolvedCurrent`, `SolvedStale`, `SolveFailed` -- every
one reached by a test section, none stored. `ThermalResultSlot` holds what a
solver service will hold, with the two transitions that matter proved:
**a failure is never a current result, and a failure never resurrects a
stale one** -- `recordFailure` does not touch the held result, and the full
sequence solve -> remesh -> fail -> fail -> succeed is measured step by step.
[STATE_MACHINE.md](STATE_MACHINE.md).

## Tests

```text
tests/thermal/ThermalDataTests.cpp              36 cases, 2349 assertions
  identity domains, widening, cross-phase        compile-time where possible
  units: types, dimensional closure, literals,
    catalog, Celsius absence, absolute temperature
  component order, derived magnitude, balance
    sign convention and normalisation
  validation: solver, transient, initial, loads,
    boundary conditions, definition per mode
  the analysis object: intent only (size mirror),
    no-op edits, add/undo/redo, save refused
  the result: eleven refusals, each its own section
  currentness: same IDs new generation; same
    content new generation; different content
    same source; the truth table (12 rows); every
    definition field edited in the document (12);
    material edit; geometry edit; viewer-only edit;
    source populated from the document
  lifecycle: failed-not-current, failed-does-not-
    resurrect, every state reached
  immutability, lookup by node, size independence
    (9 vs 178 nodes, 216 bytes either way)
  determinism over 16 evaluations

tests/compile_fail/ThermalIdMisuse.cpp          24 cases + 1 control
  12 identity conversions refused, 2 of them cross-phase
  5 canonical-record misuses refused by the units or the target type
  3 definition members that do not exist
  3 result mutations refused
  the control compiles, and ThermalAnalysisId -> ObjectId with it

tests/core/units/FormatAndCatalogTests.cpp      catalog count 54 -> 58
architecture.layering on the real tree          0 violations
```

## Zero-match protection

```text
ctest -R 'unit\.ThermalData' -N                 Total Tests: 36   (minimum 20)
ctest -R 'compile_fail\.thermalids\.' -N        Total Tests: 24   (minimum 12)
ctest -R 'unit\.ThermalInput' -N                Total Tests: 11   (minimum 8)
ctest -R 'architecture\.checker\.[a-z-]*thermal' -N   Total Tests: 4  (minimum 4)
an impossible filter                             0, so the counter is real
```

Both new filters are in `ZeroMatchGuard.cmake` with asserted minimums.

## Mutation probes

```text
run-time probes with a verdict   25   on the production tree, each rebuilt,
                                      run and restored
killed                           23   every provenance omission (mesh,
                                      material revision, analysis revision,
                                      mode), currency by id or by count,
                                      stamp check skipped, failure as
                                      current, failure discarding or
                                      resurrecting the result, every mode
                                      and value validation, swapped
                                      components, magic zero, an unordered
                                      roster, a Celsius unit in the catalog
survived                          2   M22 roster sort removed -- EQUIVALENT,
                                      Document's std::map iterates ascending
                                      by construction; M24 a Celsius Unit
                                      constant with no catalog entry --
                                      RECORDED, see limitations; its
                                      realistic form (M24b) is killed
compile-time probes              24   the compile-fail group, every build
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) carries the table with the
regression test that kills each.

## Adversarial review

```text
QUESTIONS                 42  (34 from the phase plan, 8 of the reviewer's)
FINDINGS                   2
PRODUCTION DEFECTS         0
TEST METHOD FIXED          1  the size-independence test assumed a flat block
                              could be refined by a global target; P16 had
                              measured that it cannot; the test uses a
                              cylinder
RECORDED                   1  one effective material per document (P15's
                              model, inherited)
GATE-BLOCKING              0
```

## Known limitations

```text
The roster sort in thermalAnalysisIds is redundant with Document's own
  container (std::map keyed by ObjectId iterates ascending), so a mutant that
  removes it is equivalent and survives; the sort stays as defence against a
  container change and the test cannot distinguish the two.

A Unit constant added to Units.hpp WITHOUT a catalog entry is not detected by
  the Celsius probe, because nothing in C++ enumerates a namespace's
  constants. A unit outside the catalog cannot reach a document, a parameter
  or a file -- those resolve units by catalog symbol -- so the exposure is
  code that spells it by name, which review catches. The realistic mutant,
  unit plus catalog entry, is killed.

One effective material per document (F2). Two analysed bodies in one document
  share material and materialRevision in their sources, correctly under P15's
  model.

A ThermalResult holds a steady field or one transient sample; the container
  for a transient HISTORY is P18-TRANSIENT-001's. Every sample is a complete,
  validated field bound to its mesh and its time.

A sub-zero temperature in a RESULT is refused (not merely a validation
  warning): a mathematically valid linear solution with a nonphysical node
  becomes a reported failure naming the node. Conservative by choice.

The absence of unordered containers, clocks and hashes in the data model is
  proved by construction (a trivially copyable source with an asserted size)
  and by grep, not by a test that could detect their introduction.

This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered,
then two repeat stages. One uninterrupted detached run, 21:55:13 to
01:35:57, 3h40m44s, 17 stages, `qualify.cmd exit 0`. Before it, the working
tree passed the whole suite in debug-ext (4084/4084, 1890.98 s,
`qualification/prefreeze.txt`).

```text
PRESET            CONFIGURE  CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext              0       0   27m24s       1 edge      31m54s   4084/4084
release-ext            0       0   33m31s       1 edge      28m45s   4084/4084
debug-shared-ext       0       0   27m39s       1 edge      31m53s   4084/4084

REPEAT (5x each of 1213 selected tests, back to back)
release-ext            0                            1213/1213   1092.26 s
debug-ext              0                            1213/1213   1237.26 s

warnings, all three clean builds   0   (-Werror, 665 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       11 DLLs, libbettercad_thermal.dll grown
```

**4084** is P18-ARCH-001's 4024 plus this milestone's 36 data cases and 24
compile-fail cases. **665** objects is 660 plus three thermal sources, one
test file and one compile-fail control. The data suite gives the identical
2349 assertions in 36 cases and the identical 8 message lines under `-O0 -g`,
under the optimiser and across a DLL boundary. **Predecessor regression:**
`core/Id.hpp` and the units headers are included by nearly every
translation unit, so the three clean builds and unfiltered runs are the P15,
P16 and P17 regression, and all 4024 earlier tests pass in each preset.
[FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    4084/4084 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 665 objects; 1213 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     include 5b4d1dfb, src 4fbeceef, tests de83d2e5 moved; apps,
          examples, cmake, CMakeLists.txt, CMakePresets.json identical to
          the P18-ARCH-001 freeze; identical at the freeze, before and after
          the run
EVIDENCE: this directory
ADRs:     none added; ADR-049, ADR-050 and ADR-051 unchanged
NEXT:     P18-MAT-001 — Thermal Material Resolution. NOT AUTHORIZED by this
          result; each milestone is started by the owner.
```

## Revision

First issue, 2026-10-16.
