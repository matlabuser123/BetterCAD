# P16-DATA-001 — Mesh Data Model / Identity / Units

```text
STATUS:      see RESULT, below
TASK:        P16-DATA-001 -- Mesh data model / identity / units
PHASE:       P16 -- Meshing
DATE:        2026-09-30
```

## Baseline

```text
branch        main
HEAD          9964f88e533f6b00f5ab7be42d80bb8ee8a20ecc
origin/main   9964f88e533f6b00f5ab7be42d80bb8ee8a20ecc   (HEAD == origin/main)
tree          001e3bc395fbb9c1d47666c1339fb69fc5e70399
log           9964f88 BetterCAD: define P16 meshing architecture
working tree  clean
build root    C:/Users/uqhas/AppData/Local/bc-build   (outside the synchronised folder)
compiler      GNU 16.1.0, C++23
presets       debug-ext, release-ext, debug-shared-ext
```

## Prerequisite

`P16-ARCH-001` was verified before any production code was written, not assumed:

```text
TODO.md P16-ARCH-001    25 boxes, 25 ticked, 0 open, PASS marker present
qualification           2815/2815 in three presets at 9964f88, 0 warnings
ADRs read               ADR-030, ADR-031, ADR-032, ADR-033
```

### Architecture decisions consumed

Read from the ADRs themselves, and each one constrains something below.

| Decision | Source | Effect here |
| --- | --- | --- |
| Mesh handles are strong index types, **never `bettercad::Id`**, and never widen to `ObjectId` | ADR-031 | `MeshIds.hpp`; 15 compile-fail cases |
| A handle is valid only with the `MeshId` **and generation** it came from | ADR-031 | `MeshStamp`, `Mesh::owns()` |
| Tet4 only, and an element **carries its type** | ADR-031 | `ElementType`, no Tet10/Hex8 enumerators |
| **Element Jacobians are positive, and a violation is a failure** | ADR-032 | positive signed volume; inverted elements **rejected**, never renormalised |
| A mesh has **one region per solid** and **shares no node between regions** | ADR-032 | `RegionId`; `NodeSharedBetweenRegions` |
| A mesh is in its body's frame and **carries no transform** | ADR-032 | one frame, no transform field, no per-node frame |
| No mesh, region or element holds a material value | ADR-028, ADR-032 | nothing material-shaped exists here |
| `ValidatedMesh` belongs to the mesher's **validating path** | ADR-030 | deliberately **not** built here; see Scope |

**One conflict between the brief and the qualified architecture, resolved in the
architecture's favour.** The brief suggests `using NodeId = Id<NodeIdTag>`. ADR-031 forbids
exactly that, because `Id<Tag>`'s documented contract is stable, persisted, never-reused
identity and a mesh handle is the opposite on all four counts. The brief also says not to
introduce a second generic ID template — so the handles are three **concrete** types, written
out, which satisfies both. The brief's own instruction is that the qualified architecture
wins.

## Scope

```text
IN SCOPE      mesh-local identity; node coordinates; triangle and tetrahedral
              connectivity; element typing; orientation; connectivity validation;
              geometric degeneracy; bounds; an immutable read-only mesh

NOT IN SCOPE, and NOT STARTED
              P16-GEOM-001, P16-SURF-001, P16-VOL-001, P16-SIZE-001 and later.
              Nothing here GENERATES a mesh. No OCCT is called, no backend
              exists, no geometry is read.
```

Three things are deliberately absent, each with a reason rather than an omission:

```text
ValidatedMesh    ADR-030 gives it to the validating path. Naming a data-level
                 check "solver ready" is precisely the conflation this phase
                 exists to prevent, so the report method is dataValid().
FaceName         geometry correspondence is P16-MAP-001's. A node lying on a CAD
                 face does not make the face part of the node.
adjacency        N/A -- derived, and nothing needs it. The one global check that
                 might have wanted it, no-node-shared-between-regions, is
                 computed locally inside validate() and exposes no structure.
```

## Identity

| Identity | Domain | Stable within a mesh? | Stable across remesh? | A CAD identity? | Persisted? |
| --- | --- | --- | --- | --- | --- |
| `ObjectId` | document | yes | n/a | **yes** | yes |
| `NodeId` | one mesh | yes | **NO** | **no** | **no** |
| `ElementId` | one mesh | yes | **NO** | **no** | **no** |
| `RegionId` | one mesh | yes | **NO** | **no** | **no** |
| `MeshId` | process | yes | a new mesh gets a new one | no | no |

```text
NodeId      class in bettercad::meshing, wrapping std::uint32_t.
            Not bettercad::Id. Does not widen to ObjectId. 0 is invalid;
            allocation starts at 1, matching every other BetterCAD identifier.
ElementId   the same, one identity space across Triangle3 and Tetrahedron4.
RegionId    the same. IMPLEMENTED, not N/A: ADR-032's "shares no node between
            regions" is uncheckable unless an element says which region it is in.
MeshStamp   {MeshId, generation}. Held by the MESH, not by every handle: a
            tetrahedron stores four handles and a million-element mesh stores
            four million, so a per-handle stamp would multiply connectivity
            memory for a check that belongs at the boundary.
```

Handles are **strictly increasing**. A caller may let the builder allocate or supply a handle
greater than the last of its kind. One rule buys four properties: storage stays contiguous
and ascending so lookup is a binary search; a duplicate handle is *unrepresentable* rather
than merely rejected; gaps are allowed, so a generator that skips numbers is representable;
and because gaps are allowed, lookup is forced through identity instead of indexing.

## Coordinates, units and frame

```text
representation   Point3D {Length x, y, z} -- the existing core type, reused.
                 core/geometry/Mesh.hpp already uses it, so the engineering mesh
                 and the kernel's triangulation agree on what a point is.
units            strong Length per component, SI internally. No bare double
                 crosses the public API as a coordinate.
frame            the frame of the body the mesh was built from (ADR-032). There
                 is no transform field and no per-node frame, so a mesh with some
                 nodes local and some world is not representable.
signed volume    Volume (Length^3) -- dimensionally checked, not asserted in a
                 comment.
triangle area    Area (Length^2).
```

Arithmetic is done on SI values inside `signedVolume`/`triangleArea` and wrapped once at the
boundary, which is the same thing `distance(Point3D, Point3D)` in core already does. A
`Length`-valued cross product would need an area-valued vector type that nothing else in
BetterCAD wants yet.

## Element model

```text
Node          {NodeId id; Point3D position;}     and nothing else
Triangle      {ElementId; array<NodeId,3>; RegionId}
Tetrahedron   {ElementId; array<NodeId,4>; RegionId}
ElementType   Triangle3, Tetrahedron4 -- and nothing else
```

Arity is part of the type, so an element with the wrong number of handles is mostly a compile
error and always a rejection. `Tet10`, `Hex8` and `Wedge6` are **absent rather than
reserved**: an enumerator with no arity, no orientation convention and no validation would be
a promise the code does not keep.

## Orientation and signed volume

```text
triangle normal   n proportional to (p2 - p1) x (p3 - p1), right-hand rule on the
                  STORED order. Whether it faces out of the material is decided
                  against the CAD face in P16-MAP-001; P16-DATA fixes only the
                  convention.
tetrahedron       V = 1/6 * (p2 - p1) . ((p3 - p1) x (p4 - p1))
                  POSITIVE IS VALID (ADR-032).
                  V < 0  -> InvertedTetrahedron, REJECTED
                  V == 0 -> DegenerateTetrahedron, REJECTED
                  not finite -> DegenerateTetrahedron, REJECTED
```

**No `abs()` anywhere**, and no renormalisation. ADR-032 says a violation is a failure, and
reordering an inverted element's handles would destroy the only evidence that a generator
produced one.

## Analytical fixtures

Every expected value is computed by hand in the test, never by the function under test.

| Fixture | Expected signed volume | Asserted | Result |
| --- | --- | --- | --- |
| Reference tet `(0,0,0)(1,0,0)(0,1,0)(0,0,1)` | `+1/6` exactly | within `1e-15` | **PASS**, positive |
| Same, `p2` and `p3` swapped | `-1/6` | within `1e-15`, and `== -forward` | **rejected**, `InvertedTetrahedron` |
| Coplanar `(0,0,0)(1,0,0)(0,1,0)(1,1,0)` | `0` | `== 0.0` exactly | **rejected**, `DegenerateTetrahedron` |
| Non-axis-aligned `(1,1,1)(2,3,1)(1,2,5)(4,1,2)` | `+25/6` | within `1e-14` | **PASS**, positive |
| Reference tet translated by `(12.5, -3.25, 7.75)` | unchanged | within `1e-15` | **PASS** |
| Reference tet rotated 37° about Z | unchanged | within `1e-15` | **PASS**, still positive |
| Reference tet reflected in x | `-1/6` | within `1e-15` | **PASS**, sign flipped |
| Reference tet with all four handles reversed | **unchanged** | within `1e-15` | **PASS** — even permutation, see F3 |
| Extreme but finite, coordinates `1e300` | overflows | `!isFinite` | **rejected** |

Triangle area: a 3-4-5 right triangle gives `6` exactly (within `1e-15`); three collinear
points and two coincident points both give exactly `0`.

The non-axis-aligned case exists so that an implementation which accidentally assumed axis
alignment cannot pass. Its hand computation is in the test:
`e1 = (1,2,0)`, `e2 = (0,1,4)`, `e3 = (3,0,1)`, `e2 x e3 = (1,12,-3)`, `e1 . (1,12,-3) = 25`,
so `V = 25/6`.

## Validation

Data defects, not quality findings — the boundary the header states and keeps:

```text
NonFiniteCoordinate      MissingNodeReference     RepeatedNodeReference
DegenerateTriangle       DegenerateTetrahedron    InvertedTetrahedron
NodeSharedBetweenRegions MissingRegion            EmptyMesh
```

**There is no tolerance in the validation file.** An element is degenerate when its measure is
exactly zero or not finite, never when it is thin. A sliver is a valid description of a region
of space that will solve badly, which is `P16-QUALITY-001`'s to complain about; a tolerance
here would silently become a quality threshold nobody chose.
`Validate_AcceptsAThinTetrahedronBecauseQualityIsNotDataValidity` pins that with a 1e-12 m
sliver.

Insertion fails fast and atomically; whole-mesh validation **collects every issue**, because a
mesher that produced one inverted element usually produced several.

`dataValid()` is deliberately not called `isSolverReady()`: this layer can establish that a
mesh describes a body coherently, not that it is a good discretisation of the right body.

## Ordering and determinism

```text
nodes       ascending NodeId
triangles   ascending ElementId
tetrahedra  ascending ElementId
elements    canonically: every Triangle3, then every Tetrahedron4
regions     ascending, no repeats
issues      MeshIssueKind enumeration order, then node then element handle
```

No unordered container exists in the module. `MeshValidation.cpp` uses `std::map` with a
comment saying why. `Mesh_EnumeratesNodesAndElementsInHandleOrderEveryTime` rebuilds 8 times
from sparse handles (2, 5, 9, 40) and compares the whole sequence, the bounds and the report;
`Validate_ProducesAnIdenticalReportOnRepeatedRuns` compares reports 8 times **including their
messages**.

## Mesh container and immutability

```text
MeshBuilder -> Mesh      the only construction path
Mesh                     IMMUTABLE BY API: every accessor is const and returns
                         std::span<const T>; no method mutates; build() returns
                         by value so a finished mesh cannot be changed through
                         the builder that made it
```

P17 holds a `const Mesh&` and has nothing to call that would move a node or alter
connectivity. Two compile-fail cases enforce it rather than documenting it.

```text
bounds         optional<MeshBounds>; nullopt for a mesh with no nodes, because a
               zero box at the origin is the RIGHT answer for a mesh holding one
               node at the origin and the two must stay distinguishable
empty mesh     valid as a container, reported as EmptyMesh by validation: a
               construction state is not a readiness state
equality       exact representation equality, not semantic equivalence
thread safety  a finished Mesh is immutable and may be read concurrently;
               MeshBuilder is not thread-safe by design; the MeshId counter is
               atomic because two builders must not receive the same MeshId
duplicates     a topological duplicate is NOT rejected -- deferred to
               P16-QUALITY-001 with the reason recorded at the declaration
```

## Compile-time safety

15 cases, each with a control target that compiles, so every failure is attributable to its
own line. Full table in [API_AUDIT.md](API_AUDIT.md). The three that matter most are
`node-id-as-object-id`, `element-id-as-object-id` and `region-id-as-object-id`: without them,
ADR-031's central invariant would be a sentence in a document.

One case was **removed because it could not fail**, and one **regex was wrong**; both are
written up in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Adversarial review

Full record: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
ATTACKS              23 from the brief, plus 5 raised in the review
FINDINGS             6
PRODUCTION DEFECTS   0
FIXED                6 -- all in this milestone's own tests, headers or evidence
DEFERRED             1, with a documented reason (topological duplicates)
```

The finding worth reading is **F3**: reversing a tetrahedron's four handles is the permutation
`(1 4)(2 3)` — two transpositions, an **even** permutation — so the signed volume is
**unchanged** and the element is not inverted. My fixture expected two inversions and got one;
the production code was right. It matters beyond the test, because a mesher author
"repairing" inverted elements by reversing their connectivity would accomplish nothing.
`SignedVolume_SignFollowsThePermutationParityNotTheApparentReversal` now pins reversal,
one swap and a 3-cycle against the hand-computed reference.

## Files changed

```text
include/bettercad/meshing/MeshIds.hpp          new
include/bettercad/meshing/Mesh.hpp             new
include/bettercad/meshing/MeshValidation.hpp   new
src/meshing/Mesh.cpp                           new
src/meshing/MeshValidation.cpp                 new
src/meshing/CMakeLists.txt                     new
src/CMakeLists.txt                             + add_subdirectory(meshing)
tests/meshing/MeshDataTests.cpp                new
tests/compile_fail/MeshIdsMisuse.cpp           new
tests/compile_fail/CMakeLists.txt              + the meshids group
tests/CMakeLists.txt                           + the test source and BetterCAD::meshing
```

`src/meshing/` exists for the first time, which is what makes `layer_meshing 4` — registered
by `P16-ARCH-001` — load-bearing rather than declarative.

## Regression

Harness: `qualification/qualify.cmd`, carried from P15-QUAL-001 and **fixed here** — see "The
harness defect this milestone found", below. Its own regression,
`qualification/verify-harness.cmd`, was run after the fix and passed: a failed stage still
gives a non-zero exit, and a zero-match repeat filter is still counted as a failed stage.

Pre-freeze checks, all cleared **before** the expensive run, because all three of P15's voided
qualifications came from doing a cheap check after an expensive one:

```text
git diff --check                  clean
rebuild after the last edit       exit 0, 0 diagnostics
new tests, --repeat until-fail:5  60/60, 300 executions, 433 s
debug-shared-ext build            exit 0, 0 diagnostics, no DLL-boundary diagnostics
debug-shared-ext meshing tests    45/45
architecture.layering             403 files, 0 violations
```

| Preset | Clean build | Full ctest | Tests |
| --- | --- | --- | --- |
| `debug-ext` | 20m38s | 16m30s | **2875/2875** |
| `release-ext` | 20m38s | 14m26s | **2875/2875** |
| `debug-shared-ext` | 19m58s | 24m17s | **2875/2875** |
| repeat `release-ext` | — | 7m48s | **60/60** ×5 |
| repeat `debug-ext` | — | 8m07s | **60/60** ×5 |

```text
2875 = the 2815 P16-ARCH-001 qualified, plus this milestone's 60 new tests
       (45 unit tests + 15 compile-fail cases)
9225 test executions (2875 x 3, plus 60 x 5 x 2)
0 failures
```

**0 compiler warnings in all six build and rebuild logs**, checked strictly for `warning:` and
`[-W`, so the figure is 0 out of 0 rather than 0 tolerated.

**The binaries tested are the binaries built.** Each preset's second build exited 0 having
recompiled and relinked nothing: `grep -icE "Building CXX|Linking CXX"` over all three rebuild
logs returns 0.

**The determinism stage selected 60 tests in each of two presets** — recorded in the times file
rather than implied, which matters because the previous attempt's determinism stage selected
none at all.

## The harness defect this milestone found

The first attempt at this regression **failed**, and the cause was in the harness, not the
code. It is recorded here rather than quietly replaced, because the failure mode is expensive
and will recur.

All three presets passed — 2875/2875 each, clean builds, proven no-op rebuilds — and then the
run died with `else was unexpected at this time`, exit 255, **before the determinism stage
ran**.

```text
root cause   %REPEAT% is substituted when cmd PARSES the enclosing
             for ... do ( ... ) block, not when it executes. This milestone's
             subject needs an alternation over ten test-name prefixes, so the
             filter contains | ( and ) -- which closed the block early and
             orphaned the else.

why now      the defect has been in the harness since P15-QUAL-001 and survived
             because every earlier milestone's subject was a single word:
             "architecture", "cli.material". Nothing before needed an
             alternation.

why it hurts it takes out the LAST gate AFTER all the expensive ones have
             passed, and a reader of the times file sees three green presets and
             no determinism line.

fix          !REPEAT! -- delayed expansion, substituted after the block is
             parsed. Three uses, with the reason recorded in the harness header
             so it propagates to every milestone that copies it forward.
```

Both checks before re-running, and both cheap:

```text
verify-harness.cmd                   PASS -- the harness still reports failures
the exact filter that broke it       PARSES -- run against a nonexistent preset,
                                     so it cost seconds. The repeat block now
                                     executes and records the filter verbatim,
                                     pipes and parentheses included.
```

**Re-run in full rather than resuming.** The three preset stages were not void — only `docs/`
had changed, which is outside the eight-path source fingerprint — and running just the missing
determinism stage against the existing binaries would have taken about fifteen minutes. It was
re-run from clean anyway, because that turns a claim about binary identity into a single
harness verdict, and one coherent verdict is what every other milestone's evidence shows.

## Result

```text
RESULT:      PASS
HARNESS:     "Qualification passed: every stage exited 0."
ELAPSED:     2h13m14s (19:01:21 -> 21:14:35, 2026-09-30)
TESTS:       2875/2875 in each of three presets; 9225 executions; 0 failures
DETERMINISM: 60 tests x5 in release-ext and debug-ext; 0 failures
WARNINGS:    0 in all six build and rebuild logs
TREE:        the eight qualified tree IDs are identical before the first build
             and after the last test run
EVIDENCE:    qualification/qualification-times.txt and the per-preset logs
```

Qualified source trees, recorded before the first build and unchanged after the last test run:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           ea8a82906a0c75f2d97b377ce63e0eefc6ce5fbd
src               77fe2ab07cc5d6b667fc8f6f400d3b1aa6806fa9
tests             1d81c8e0620410c430f93d43a5786e011821fd89
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt    13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`docs/` and `TODO.md` are outside the fingerprint, so the evidence and the TODO closeout that
follow this run do not affect it.

## Known limitations

```text
Nothing here generates a mesh. The model can be built by hand and validated; the
first mesh from geometry is P16-SURF-001's.

Tet4 only, and Tet4 is not enough for accurate bending stress (ADR-031). Enough
for the pipeline; the element type is a tag so Tet10 is an addition.

A topological duplicate -- two tetrahedra on the same four nodes -- is not
rejected. Deferred to P16-QUALITY-001 with the reason recorded at the
declaration.

No geometry correspondence, no material, no persistence, no staleness. Each
belongs to a named later milestone and none is stubbed here.

F6's three semantics are documented rather than enforced; equality and thread
safety are contracts, not checks.
```

## Revision

First issue, 2026-09-30.
