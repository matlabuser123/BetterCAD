# P16-REFMOD-001 — Meshing Reference Models

```text
STATUS:   PASS
DATE:     2026-10-06
GATE:     reference suite defined + 8 models committed and executed
          + only valid positively oriented Tet4 + analytical CAD-volume checks
          + hole and void preserved + boundary conformity and mapping
          + local refinement demonstrably targets F + quality run on every mesh
          + rigid-transform invariants + model- and settings-change remesh
          + save/load/regenerate + CLI and CLI/core equivalence
          + determinism + adversarial review + mutation protection
          + three presets + full regression + clean-tree verification
```

```text
BASELINE:     a6113de  (P16-CLI-001), working tree clean, HEAD == origin/main
              TREE bd99a98d6b785c3e014740655a030ba4d0cd2220
SCOPE:        a committed, deterministic, independently validated meshing
              reference suite: eight model IDs in nine documents, their
              analytical oracles, and the gates that make them qualification
              references rather than demos. No new meshing capability, no new
              CLI command, no new document format.
```

## Prerequisites

Read from each milestone's recorded RESULT, not from a checkbox.

```text
P16-ARCH-001     PASS      P16-QUALITY-001  PASS
P16-DATA-001     PASS      P16-MAP-001      PASS
P16-GEOM-001     PASS      P16-VIZ-001      PASS
P16-SURF-001     PASS      P16-CMD-001      PASS
P16-VOL-001      PASS      P16-PERSIST-001  PASS
P16-SIZE-001     PASS      P16-CLI-001      PASS
INFRA-NETGEN-001 PASS -- Netgen v6.2.2604 QUALIFIED
```

```text
Netgen        6.2.2604
OCCT          8.0.1
compiler      GCC 16.1.0, WinLibs POSIX UCRT
build         CMake 4.4.1 / Ninja, presets debug-ext, release-ext,
              debug-shared-ext, BETTERCAD_BUILD_ROOT outside the source tree
```

## What was built, and what was reused

The audit came before any production code, and its finding changed the shape of
the work: **BetterCAD already has a reference-model system, and it is on its
fifth suite.** P13, P14 and P15 each added the same five things to
`examples/reference_models` and `tests/reference`, and P16 adds exactly those
five and nothing else.

```text
NEW
  examples/reference_models/MeshReferenceModels.hpp   the declaration
  examples/reference_models/MeshModels.cpp            the builders
  examples/reference_models/MeshCatalog.cpp           kind -> document
  tests/reference/MeshTestSupport.hpp                 the shared checks
  tests/reference/MeshModelsTests.cpp                 28 cases

EXTENDED
  examples/reference_models/main.cpp        a --meshes loop, which is also the
                                            CLI fixture and the evidence
                                            generator
  examples/reference_models/CMakeLists.txt  three sources, BetterCAD::meshing
  tests/reference/Analytic.hpp              an mm-based P16 section: the
                                            plate-with-hole volume and the
                                            INSCRIBED-POLYGON BOUNDS
  tests/CMakeLists.txt                      the suite, and 25 fresh-process
                                            CLI fixtures
  tests/cli/ZeroMatchGuard.cmake            this milestone's two filters

PRODUCTION FIX
  include/bettercad/meshing/MeshSizing.hpp  ResolvedSizing::unresolvedCount()
  src/meshing/MeshSizing.cpp                inline, so it is usable across a
                                            DLL boundary at all
```

Detail: [AUDIT.md](AUDIT.md).

## Reference suite contract

Eight mandatory model IDs in nine documents — RM-MESH-06 is a pair, because
comparing a body with the same body rigidly transformed *is* the case, exactly
as RM-MAT-04 is three entries for one ID.

Every model declares its geometry, its closed-form volume, its meshing intent,
its expected boundary regions and its expected outcome; every one carries that
intent as a `MeshControl` document object, **RM-MESH-08 included** — without one
its headless refusal would name a missing control rather than the broken body,
which is the defect P16-CLI-001's mutation M6 found.

Full statement: [REFERENCE_MODELS.md](REFERENCE_MODELS.md).

## Analytical oracles

```text
the model's DIMENSIONS, read from its own parameters
    -> a closed form that knows nothing about BetterCAD
    -> compared with the catalog's DECLARED volume   (a typo in either)
    -> compared with the KERNEL's CAD volume         (OCCT, independently)
    -> compared with the TETRAHEDRAL volume          (the real gate)
```

`tests/reference/Analytic.hpp` includes no BetterCAD and no OCCT header, so it
**cannot** call a production volume function — the brief's critical rule is
structural here rather than a matter of discipline.

For the curved models there is a **derived two-sided bound** instead of a
tolerance: an inscribed chord polygon contains the disc of radius `r - d` and
lies in the disc of radius `r`, where `d` is the declared deflection. Nothing is
fitted, no segment count has to be known, and the **direction** of the error
follows from which side of the material the curved surface is on — which is why
RM-MESH-03's mesh must come out ABOVE its analytic volume while RM-MESH-02's and
RM-MESH-04's come out below.

Formulas, the tolerance table with every measurement beside its gate, and the
convergence study: [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Results

Every figure is printed by the suite or the runner.
Tables: [RESULTS.md](RESULTS.md). Raw runner output:
[qualification/reference-run-debug-ext.txt](qualification/reference-run-debug-ext.txt).

```text
MODEL        OUTCOME   NODES  TET4  REL VOLUME ERROR   INVALID
RM-MESH-01   mesh          8     6  1.980e-16                0
RM-MESH-02   mesh        140   508  5.069e-03                0
RM-MESH-03   mesh         81   126  6.770e-04                0
RM-MESH-04   mesh        261   808  5.069e-03                0
RM-MESH-05   mesh          9    12  1.263e-16                0
RM-MESH-06   mesh      9 x 2 12 x 2 2.450e-16 / 1.225e-16    0
RM-MESH-07   mesh         61   267  1.213e-16                0
RM-MESH-08   REFUSAL     n/a   n/a  n/a                    n/a

8 model IDs expected, 8 executed, 8 meshed, 1 refused, 9 documents
```

```text
ELEMENT ORIENTATION    1751 positive, 0 zero, 0 negative, 0 non-finite
MINIMUM TET VOLUME     > 0 and finite in every model; smallest 0.202694 mm^3
BOUNDARY CONFORMITY    every model: conforms, 0 unmatched either way
MAPPING                every model: complete, 0 unmapped, 0 attributed twice,
                       0 faces without facets, 0 UNNAMED faces
VOID OCCUPANCY         0 violations, on RM-MESH-03 and RM-MESH-04, and again on
                       RM-MESH-03 after its geometry edit
QUALITY                every model: structurally valid, 0 invalid, policy
                       satisfied, under P16-QUALITY-001's own report-only
                       default -- no reference-model threshold invented
```

## The three findings worth a reviewer's attention

**The pre-freeze shared build found a production defect for the second milestone
running.** `ResolvedSizing::unresolvedCount()` is a public API method of a
non-exported data struct, defined out of line — so it has been unusable across a
DLL boundary since P16-SIZE-001, and nothing noticed because nothing outside the
meshing library had called it. Defining it inline, as every other predicate on a
report struct in that module already is, was the fix. Counting the unresolved
controls in the test instead would have hidden a real product defect.

**The convergence table first passed on floating-point noise.** Deflections of
0.5 and 0.25 mm produce the *same* 140 boundary facets, because OCCT's
20-degree angular limit binds at both, so the two volumes differed only in their
last two digits and "the error fell" was a one-ULP accident. The suite now
asserts strictly increasing facet counts **before** comparing errors, and the
levels were changed to ones that genuinely differ.

**Local refinement cannot be measured at the refined face's own facets.** A
planar face has two boundary triangles whatever the deflection, so the elements
owning them span the face however fine the interior is — measured, they keep a
57.85 mm median edge in a mesh whose element count the control multiplied by
twenty-two. The instrument is a band in the **volume** near the face, and the
decisive test is the **mirror**: a third mesh refining the opposite face, so the
same band can be compared between the two. Mutation M5 refines the whole body
and the "target is refined" assertions do not fire; only the mirror catches it.

## How to run the suite

One invocation runs every RM-MESH case:

```text
bettercad_tests "[refmod][mesh]"            28 cases, 23410 assertions
```

and through ctest, where tags are not selectors, two name filters do it:

```text
ctest -R "unit\.Mesh(Block|Cylinder|PlateWithHole|Tube|ThinPlate|
                     TransformedBlock|LocalRefinement|OpenProfile|
                     Reference|Curved)"     28, in process
ctest -R "refmod\.mesh\."                   25, fresh processes
```

No parallel naming scheme was introduced for this. Every case's name begins
with `Mesh`, as the repository's `<Subject>_<Behaviour>` convention gives it, so
`unit\.Mesh` is a superset filter that also sweeps the meshing module — which is
what `run-qualification.cmd` uses, and what P16-QUAL-001 can use unchanged. Both
filters are in `ZeroMatchGuard.cmake`'s table with asserted minimums, because
`ctest -R` exits 0 when a pattern matches nothing.

The models themselves, as nine `.bcad` documents, come from

```text
bettercad_example_reference_models --meshes [--out <dir>]
```

whose own exit code is an assertion: it fails if any model does not build, does
not regenerate as expected, meshes when it must not, fails to mesh when it must,
or publishes a mesh after a refusal.

## Determinism

Five runs each, to P16-VOL-001's own standard — counts, the volume **bitwise**,
the sizing restrictions, and the element connectivity index for index — plus the
canonical node set, the quality report's values **and order**, and the mapping's
completeness.

```text
RM-MESH-01, 03, 06, 07   5 runs each, stable on every compared quantity
all nine documents       3 runs each, outcome stable, RM-MESH-08's diagnostic
                         identical every time and nothing published
```

## Cross-preset equivalence

```text
PRESET            REFERENCE SUITE             CLI FIXTURES
debug-ext         28 cases, 23410 assertions  25 / 25
release-ext       28 cases, 23410 assertions  25 / 25
debug-shared-ext  28 cases, 23410 assertions  25 / 25
```

## Three-preset qualification and full regression

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0          (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3381/3381
release-ext            0       0      0         0           0   3381/3381
debug-shared-ext       0       0      0         0           0   3381/3381

REPEAT (5x each of 692 selected tests, back to back)
release-ext            0                            692/692   686.16 s
debug-ext              0                            692/692   721.74 s

warnings, all three clean builds   0   (-Werror, 595 objects each)
no-op rebuilds                     0 compiles, 0 links
shared build                       9 DLLs linked
```

Unfiltered, after a fresh configure and a clean build in each preset. 17 stages,
2 h 38 min, one uninterrupted detached run.

**3381 of 3381 in every preset, with no exception and no footnote** — the two
preceding milestones each had to explain one failure here, the
`cli.new.unicode-path` code-page artefact, because their pre-freeze runs were
made outside `chcp 65001`. The pre-freeze gates set it this time, so the
pre-freeze and qualification figures are the same number.

## Clean-tree verification

A real clean checkout of exactly the frozen tree — `git checkout-index` from the
same scratch index the fingerprint is computed from, so no build output and no
untracked stray comes with it.

```text
frozen tree             c932abc0557d9644c33566eda3db63c7ac34b36f
files checked out       3380
build outputs present   0
configure               exit 0
build                   exit 0, 0 warnings, 595 objects
reference suite         28 cases, 23410 assertions, exit 0
refmod CLI fixtures     25 / 25
```

It took two attempts and a corrected diagnosis: CMake's bundled curl has no CA
trust anchors in this environment, so a build root with no populated `_deps`
cannot fetch the pinned archives at all. Detail, and what that leaves
unverified, in [FREEZE.md](FREEZE.md).

## Result

```text
RESULT:    PASS
GATE:      met -- eight model IDs in nine committed documents, every one
           executed and every one's outcome what it declares; every expected
           value derived from the model's own dimensions through a closed form
           that cannot call BetterCAD; every structural fact recomputed from
           the mesh's own nodes and elements; the hole and the void proved empty
           by an independent occupancy test; local refinement proved by a
           mirror rather than by an element count; the rigid transform's
           invariants held with its two node populations judged separately;
           intent persisted and the mesh not; the CLI agreeing field for field
           in a separate process; and RM-MESH-08 refused with nothing published
EVIDENCE:  this directory; qualification/qualification-times.txt for the run,
           qualification/mutation/README.md for the mutations,
           qualification/cleantree.txt for the clean-tree build
TREE:      c932abc0557d9644c33566eda3db63c7ac34b36f, qualified and committed
```

Identical, **and that includes the element counts and the volume doubles**: the
CLI fixtures assert exact node, element and facet counts and exact SI volumes,
and the same regexes pass under `-O0 -g`, under the optimiser and across a DLL
boundary. For these models Netgen's output is byte-deterministic across presets.
Recorded as a measurement, not promoted to a general guarantee.

## Adversarial review

```text
QUESTIONS:           25 from the brief, plus 4 of the reviewer's own
FINDINGS:            5
PRODUCTION DEFECTS:  2 -- both fixed
SUITE DEFECTS/GAPS:  3 -- all fixed
VERDICT:             PASS
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Mutation testing

```text
9 mutations: 8 KILLED, 1 proven-equivalent survivor
```

| # | Mutation | Verdict |
| --- | --- | --- |
| M1 | RM-MESH-01 is a cube | killed, 4 |
| M2 | RM-MESH-03's hole is filled | killed, 11 |
| M3 | RM-MESH-04's walls are swapped | killed, 8 |
| M4 | the rigid transform is not applied | killed, 5 |
| M5 | the local slab swallows the whole body | killed, 8 |
| M6 | RM-MESH-08's profile is closed | killed, 7 |
| M7 | RM-MESH-05 is dropped from the catalog | killed, 7 |
| M8 | `abs()` on the tetrahedral volume | equivalent, proven |
| M9 | RM-MESH-01 is 0.7% too long | killed, 1 |

**M3 and M5 are the two that justify the suite's design.** M3's swapped walls
leave the facets disjoint, so the disjointness checks pass and only the node
radii and the normal directions catch it. M5's whole-body refinement does refine
the target, so the refinement checks pass and only the mirror catches it. Both
would have survived a suite built on counts.

**M8 is proven, not excused:** `generateVolumeMesh` runs `validate` and refuses
a zero or negative signed volume at line 418, and sums the volume at line 431,
so every term is strictly positive and `abs(x) == x`. The premise is measured —
1751 elements, 0 non-positive — and the check that would distinguish them exists
and compares the stored volume with the suite's own **signed** sum.

Detail, including a process defect in the harness itself:
[qualification/mutation/README.md](qualification/mutation/README.md).

## Known limitations

```text
RM-MESH-09, a configuration-driven model, is DEFERRED with its reason.
  `ConfigurationOverrideActive` is the FIRST check requireMeshableGeometry
  makes: meshing under an active parameter override is a refusal P16-GEOM-001
  was built to make, not a workflow P16 supports. A reference model exercising
  it would either duplicate GeometryPreparationTests' own unit or reproduce the
  historical stale-configuration bug as a supported workflow, which the brief
  forbids. Adding it is a scope decision about the CONFIGURATION contract.

A planar body's mesh quality cannot be improved by any control P16 offers.
  OCCT triangulates a planar face with two triangles whatever the deflection,
  and the volume target cannot subdivide a boundary. Measured on RM-MESH-05: a
  1 mm target on a 1.5 mm plate gives 24 elements and a slightly WORSE radius
  ratio. The suite records all three planned cases rather than hiding the
  result. Refining a planar boundary is not a P16 capability.

tetrahedralVolume(const Mesh&) has no test for its documented sign promise.
  The free function is callable on a hand-built Mesh, and its header promises
  that "an inverted element must drag the total down". Mutation M8 survives the
  whole repository for want of a test that builds such a mesh. The code is
  correct; this is a test-coverage gap in P16-VOL-001's own file, and closing it
  is one small unit test there rather than a reference model here.

RM-MESH-06's equal element counts are a property of its configuration.
  Measured, the same model at a = 110 mm gives 6 tetrahedra in one placement and
  12 in the other. Netgen's interior point is not equivariant under rotation;
  the suite asserts the equality where it holds, judges boundary and interior
  nodes separately, and says which is which.

The dependency DOWNLOAD path is unverifiable in this environment.
  CMake's bundled curl has no CA trust anchors here, so a build root with no
  populated `_deps` cannot fetch the pinned archives. The clean-tree check
  therefore supplies the same SHA256-verified sources from the local cache: the
  frozen SOURCE tree is fully verified, and the download is not. Nothing in
  BetterCAD is substituted, and the versions are pinned by URL and hash in the
  tree itself. A machine with a working trust store would close this.
```

## Documents

```text
AUDIT.md                        what already existed, and the decisions it
                                forced -- written before any production code
REFERENCE_MODELS.md             each model: geometry, oracle, intent, regions,
                                expected outcome, and RM-MESH-09's deferral
ANALYTICAL_VALIDATION.md        the formulas, the tolerance table with its
                                measurements, the derived bounds, convergence
RESULTS.md                      every measured table
ADVERSARIAL_REVIEW.md           25 + 4 questions, 5 findings
FREEZE.md                       the qualified tree, and that it is the
                                committed tree
qualification/                  the runner's output and the harness logs
qualification/mutation/         the mutation record
```
