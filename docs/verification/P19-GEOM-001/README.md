# P19-GEOM-001 — Finite-Volume Geometric Metrics

```text
TASK:            P19-GEOM-001 — Finite-Volume Geometric Metrics
AUTHORIZED:      2026-10-31 by the owner's "start", with the twenty-one item
                 checklist quoted. Recorded in TODO.md in five places before any
                 implementation began.
IMPLEMENTATION:  include/bettercad/cfd/CfdGeometry.hpp + src/cfd/CfdGeometry.cpp
                 (ADR-065); core/math/Vector.hpp gains AreaVector3D, the affine
                 difference Point3D - Point3D, and magnitude/dot for Translation3D
TESTS:           31 unit cases; 29 compile-fail cases; the full suite and the
                 repeat set on three presets
VALIDATION:      closed forms (the unit tetrahedron, a 3-4-5 triangle, an
                 analytic face pair fixing cos = 3/5 and skewness = sqrt(10)/15),
                 the divergence theorem per cell, the box surface 2(ab+bc+ca) from
                 the model's own parameters, OCCT's own volume, and the quality
                 figures P19-MESH-001 recorded before this milestone existed
RESULT:          PASS — 17/17 stages exit 0; 6058 x 3 presets; 2964 x 5 x 2
EVIDENCE:        this directory
TODO:            twenty-one boxes, only on PASS
```

---

## SCOPE

What this milestone adds, and nothing else: the geometric quantities of a
`CfdTopology` that was derived from a mesh holding a `CfdMeshContract`.

```text
per cell    volume, centroid, signed surface-vector sum, closure residual
per face    area vector Sf, area, unit normal, centroid,
            owner-to-neighbour vector, cell-to-face vector,
            orthogonal distance, orthogonality cosine,
            non-orthogonality angle, skewness
per bdy fc  outward normal
aggregate   a measured summary with no threshold in it
```

Out of scope, and absent: any acceptance bound on a quality metric
(P19-VALID-001), field storage (P19-DOF-001), boundary conditions (P19-BC-001),
gradients, matrices and solves.

## BASELINE

`HEAD` at `16cd745`, which is P19-TOPO-001's "record the commit". Before this
milestone the chain stood at:

```text
ADR-063 / P19-MESH-001   a P16 Tet4 mesh IS the finite-volume mesh; nine refusals
ADR-064 / P19-TOPO-001   the compact topology: CellIndex, FaceIndex,
                         BoundaryFaceIndex, PatchIndex; eight refusals
```

`requireCfdTopology` named the cells and faces and said which cells sit on either
side of each one. It holds no coordinate, so nothing yet said how big a cell was.
Both predecessors stated explicitly that the metrics were this milestone's, and
P19-MESH-001's own test code left a note that is now discharged: *"P19-GEOM-001
will compute them in production and must agree."*

## ARCHITECTURE

ADR-065. Three candidates were compared for the shape (a derived value, free
functions computed per call, methods on `CfdTopology`) and the derived value was
chosen: it is the third in this module's chain, borrows nothing, carries the
`MeshStamp` it was computed from, is never persisted, and gives the identities one
place to be true.

Seven decisions, of which three are worth restating here:

- **the area vector is primary**, and the area and unit normal are derived from
  it, so `|Sf|` and `Sf/|Sf|` are exactly consistent and the four area vectors of
  a tetrahedron still cancel to rounding rather than to the error of a square
  root and a division;
- **`AreaVector3D` lives in `core`**, as the fifth sibling of `Translation3D`,
  `Force3D`, `Traction3D` and `Moment3D`. A review found the ADR's first draft
  comparing only two candidates and never naming the third — `cfd::CartesianVector<Area>`,
  the typed-3-vector template that already exists — which is the one CLAUDE.md's
  "prefer extending an abstraction that is already there" rule points at. The
  decision survives on the layering: `meshing` is layer 40, cannot include cfd,
  and `meshing/SurfaceMesh.hpp` is doing this exact arithmetic untyped today;
- **measured, never judged.** The summary carries totals and extremes and no
  threshold. P19-MESH-001's owner finding F1 records that the eight P16 reference
  meshes are poor finite-volume meshes; this milestone measures that more
  precisely and refuses none of them.

## BLAST RADIUS

```text
direct callers of the new API   none yet. P19-DOF-001 is the first.
core/math/Vector.hpp            LAYER 0, so every module recompiles. Every change
                                is additive or the removal of something with no
                                caller, so no existing behaviour can move. The one
                                new operator that could have altered an existing
                                call site is Point3D - Point3D, which did not
                                compile before, so no call site can change meaning.
                                `dot` and `magnitude` are new names in namespace
                                bettercad; nothing called them with these argument
                                types, and CfdData.hpp's `magnitude()` is a member,
                                unaffected.
serialization                   none. Nothing is serialized and no format moved.
                                No cfd type reaches src/io or apps (checked).
regeneration / undo             none. Derived state keyed to a MeshStamp.
stable references               none. A geometry index is not a persistent identity.
the CLI                         none.
the reference models            unchanged.
registration                    four points: src/cfd/CMakeLists.txt,
                                tests/CMakeLists.txt,
                                tests/compile_fail/CMakeLists.txt,
                                tests/cli/ZeroMatchGuard.cmake
```

**The layer-0 change is why this milestone's repeat set is larger than
P19-TOPO-001's.** Nine core math/units prefixes were added — RigidTransform,
Transform, Point, Direction, Frame, Unit, Dimension, MassProperties, SurfaceMesh
— 94 tests. That is the blast radius deciding the regression set rather than
proximity to the edited file.

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdGeometry.hpp      the public header
src/cfd/CfdGeometry.cpp                    three passes, all in index order
include/bettercad/core/math/Vector.hpp     AreaVector3D; Point3D - Point3D;
                                           magnitude and dot for Translation3D
```

Three passes: the cells (volume and centroid), the faces (area vector, centroid,
and the relational metrics the summary needs), then the cells again for the
surface-vector sums, which need the faces' area vectors. Every sum runs in
ascending index order, which is part of the answer and not a detail.

The cell volume is **not** re-implemented: `meshing::signedVolume` is called, so
there is one formula for a tetrahedron's volume in the repository and the sign
convention cannot drift between the mesh that validated it and the solver that
integrates over it.

Three additions to `core/math/Vector.hpp` were written and then **removed** before
the freeze because nothing used them: `AreaVector3D::along`, the binary
`operator-(AreaVector3D, AreaVector3D)`, and `operator*(double, Translation3D)`.

## TESTS

```text
unit.CfdGeometry_            31 cases
compile_fail.cfdgeom.        29 cases, plus a control that must compile
```

The unit cases fall into three groups, because the three claims are different in
kind: the formulas are right (closed forms on shapes known by hand), the assembly
is right (every structural claim on every reference mesh), and the numbers agree
with an oracle.

`checkStructure` runs **every** structural claim on **every** mesh it is given —
all eight reference meshes and each hand-built fixture. That is P19-TOPO-001's
lesson: a review there found each structural property being checked on the one
model chosen for it, which left the transformed body untested.

What it checks independently rather than against the builder: every centroid is
recomputed from the mesh's own nodes; every area is recomputed from the area
vector's components and again through `meshing::triangleArea`, a separate
implementation; each cell's volume is checked a second time by the divergence
theorem over its own faces; the orthogonal distance is checked against the two
apex heights measured from the nodes, on every face; and a boundary normal is
checked against the owner's centroid, which is independent of the winding the
builder used.

### The compile-fail group

29 cases proving that the quantities do not interconvert — a volume is not an
area, `|Sf|` is not a length, `d . Sf` is a volume and not an area, a point
difference is not a point — and that two things do **not exist**: the summary has
no verdict, threshold or grade, and there is no `CfdGeometryProblem` enum. An ADR
saying a thing does not exist is not the compiler saying so.

## INDEPENDENT VALIDATION

The oracles, with their real independence stated rather than assumed. A review
found the first version of this list calling all of them independent;
**provenance is not independence**, because a number computed by another
milestone from the same formula agrees however wrong the formula is.

```text
ORACLE                      INDEPENDENT?   WHAT IT COVERS
analyticVolumeMm3           yes            a closed form over the model's own
                                           dimensions, read from its parameters
OCCT's cadVolume()          yes            the kernel integrating the exact
                                           solid; nothing discretised
2(ab + bc + ca)             yes            THE ONLY oracle for the face AREAS: a
                                           volume can be right while every area
                                           is wrong
the divergence theorem      yes            3V = sum (C_f - C_P) . Sf_out per
                                           cell, from the FACE metrics rather
                                           than the determinant
closed forms by hand        yes            the unit tetrahedron; a 3-4-5
                                           triangle; an analytic face pair
tetrahedralVolume()         NO             calls the same meshing::signedVolume
                                           over the same elements in the same
                                           order. A PLUMBING check: every cell
                                           visited once, none double-counted
boundaryVolume()            adds nothing   generateVolumeMesh already gates
                                           |tet - enclosed|/enclosed <= 1e-9
P19-MESH-001's recorded     partly         a different code path and a previous
quality figures                            milestone, but the SAME definition
```

### The closed form the quality metrics were missing

A test-gap review found that the non-orthogonality angle had exactly one analytic
anchor — zero, on the perfectly orthogonal fixture — and that the **skewness had
none at all**: both were validated only against a second transcription of the same
formula. `CfdGeometry_AnAnalyticFacePairFixesTheAngleAndTheSkewnessInClosedForm`
fixes both by hand:

```text
shared face      (0,0,0), (1,0,0), (0,1,0) in z = 0; centroid (1/3,1/3,0); n = +z
owner            apex (0,0,-1)   centroid (1/4,1/4,-1/4)   V = 1/6
neighbour        apex (0,4,2)    centroid (1/4,5/4,1/2)    V = 1/3
d = C_N - C_P    (0, 1, 3/4)     |d| = 5/4 exactly         d . n = 3/4
cos(theta)       (3/4)/(5/4) = 3/5          EXACTLY        theta = 53.13010235...
crossing         t = 1/3, at (1/4, 7/12, 0)
skewness         (sqrt(10)/12)/(5/4) = sqrt(10)/15 = 0.21081851067789195
```

A 3-4-5 ratio, so the cosine is 0.6 — a number this code could not have produced
by accident.

### The cross-milestone oracle

P19-MESH-001 measured the two quality metrics in its own test code and recorded
the numbers in its evidence before this milestone was authorized. Every one of the
32 values was verified against
`docs/verification/P19-MESH-001/local-run-2026-10-30/quality-f1-65001.log` and all
three of its preset variants, which are identical:

```text
MODEL                  interior   max deg   mean deg   max skew
RM-MESH-01 Block              6     64.09      47.56     0.5376
RM-MESH-02 Cylinder         946     89.28      57.89     4.6077
RM-MESH-03 PlateWithHole    172     84.07      56.70     6.5099
RM-MESH-04 Tube            1472     85.20      55.27     3.7138
RM-MESH-05 ThinPlate         18     88.81      66.48     0.8206
RM-MESH-06 TransformedBase   18     66.00      47.04     0.8206
RM-MESH-06 TransformedPlaced 18     66.00      47.04     0.8206
RM-MESH-07 LocalRefinement  528     75.58      31.14     1.3109
```

The production metrics reproduce all 32 to the precision of the record. The
tolerances are the half-width of the 2- and 4-decimal-place records, not a
looseness in the computation.

## ADVERSARIAL REVIEW

Two independent reviews ran before the freeze — one adversarial review of the
implementation, one test-gap analysis — and both found real defects. Two rounds
were needed, because the first round's fixes were themselves incomplete.

### A hard gate failure, which a green test run concealed

The `cfdgeom` compile-fail **control target** did not compile: its eight helper
functions are called only from inside `#if defined(BETTERCAD_CF_...)` blocks, so
in the control configuration all eight are unused anonymous-namespace functions
and `-Wunused -Werror` makes each a hard error. The control has no
`EXCLUDE_FROM_ALL`, so `cmake --build --preset <any>` failed for the whole
project, and **every one of the seventeen qualification stages would have died at
its build step**.

`ctest -R compile_fail.cfdgeom.` had reported 29 of 29 passing — because the
control is a build target that ctest never builds. A green test run was consistent
with a project that did not build.

### A claim written without the work behind it

ADR-065's Decision 5 stated that the builder's retained guards were unreachable,
and added: *"an attempt to construct a mesh that yields a topology and reaches one
of them is recorded there, and it did not succeed."* **No such attempt had been
made.** It was a prediction written as a result.

A review made the attempt and succeeded on its first candidate. Running the test
that resulted turned up a third guard, and the re-verification round a fourth.
**Four of the guards are reachable**, each reproduced independently here:

```text
GUARD                      WITNESS                                        STATUS
a face area is zero        three vertices at 0, v, 2v are EXACTLY         tested
                           collinear, so every minor is x(2y) - y(2x) = 0.
                           The degenerate cell's exact volume is zero and
                           its computed volume is rounding noise ADR-064
                           accepts when positive: +4.6259292692714853e-18
a centroid is not finite    four coordinates at 1.2e308 are each FINITE,   tested
                           which is all MeshBuilder requires, and their
                           sum overflows at the SECOND addition. The
                           determinant stays finite (+1.67e299) because
                           the magnitude enters each term only linearly.
                           THE ONE WITNESS THAT IS NOT A ROUNDING
                           ACCIDENT. A control at 1e300 builds.
an orthogonal distance     two cells 1e-18 m from their shared plane but   tested
is <= 0                    far apart in it: d . n sits two orders below
                           the rounding error of differencing the
                           centroids. 28 of 157 family members
a face has no finite       the same configuration drives d . Sf to zero.   tested
skewness                   1 of 157 — found by RUNNING the test above
                           rather than by reasoning about it
```

The error in the reasoning is recorded in the ADR because it is easy to repeat:
`V > 0 => A > 0` is proved **of the exact values**, and a guard only ever sees
computed ones.

Three guards remain genuinely unreachable and are argued, not asserted: the cell
volume (the assembler calls the same `meshing::signedVolume` on the same node
order ADR-064's gate called, so the comparison is bit-identical), the angle (it is
`arccos` of a clamped value, and a zero `|d|` forces `d . n == 0` exactly so the
distance guard fires first), and the element and node lookups.

### The API change the review forced

ADR-065's Decision 7 told consumers to form the non-orthogonality cosine
themselves — "divide `orthogonalDistance` by the magnitude of `ownerToNeighbour`,
well-conditioned everywhere" — and declined to add an accessor "for a quantity
that is already one division away". The suite's own clamp fixture proves that
expression returns `1.000000000000000222` on the most regular interior face a
tetrahedral mesh can have: one eps above one, outside the domain of `acos`. The
recommendation handed back a number no inverse trigonometric function accepts, and
nothing tested the path it recommended.

The division is now `orthogonalityCosine(FaceIndex)` with the clamp inside it,
`nonOrthogonality` is its `arccos`, and there is one clamp in the module.

### Six assertions a hard-coded zero would have satisfied

The test-gap analysis found that `cellSurfaceVectorSum` — the milestone's headline
accessor — had its **value read nowhere** in the suite; only its presence was
required, and the dimensionless ratio derived from it was bounded four orders of
magnitude above the quantity. `return AreaVector3D{}` would have survived every
case. The same held for `boundarySurfaceVectorSum`, `domainClosure`,
`worstCellClosure`, `worstBoundaryNonOrthogonality` and both orthogonal-distance
minima.

All are now compared **exactly** against the test's own re-accumulation. The
exactness is legitimate and is stated where it is claimed: the test replicates the
production accumulation in the same order over the same values, and floating-point
addition cannot be reordered without `-ffast-math`. The two angle extremes are
exact for a different reason — the maximum commutes with a monotone unit
conversion — and the comment says so.

### A mechanism disproved by measurement

`kCosineInvariance` was justified by "the cosine's error is of order `eps / cos`",
which takes a *relative* error of `d . n` and uses it as an *absolute* bound on
the quotient; divide through and the `1/cos` cancels. Measured here rather than
argued:

```text
TEST A   one fixture translated along (1,1,1), cos and |d| held fixed
  translation   L/|d|     error in the cosine
  0             3.6       0
  1 m           8.8       0
  100 m         6.3e2     2.9e-15
  10 km         6.3e4     8.2e-13

TEST B   fixed translation, cos driven down by shearing the apexes
  cos      L/|d|     error in the cosine
  0.9918   4.0       0
  0.3545   2.4       5.6e-17
  0.0920   2.1       1.4e-17        <- a tenfold fall in cos does NOT raise it
```

So the absolute error is of order `eps * L / |d|`, the bound is calibrated to
models near the origin, and Decision 7 had been contradicting itself — stating the
wrong form for the cosine and the right `eps * L / (d . n)` for
`orthogonalDistance` four paragraphs later.

### Defects in the fixes themselves

Three of this milestone's own corrections were wrong and were caught by its tests
or the second review round, and they are listed because the lifecycle is what
caught them:

```text
a dropped field declaration   a doc-comment patch deleted
                              `Length smallestOrthogonalDistance{};` and left the
                              comment orphaned; debug-shared-ext caught it
the covariance sweep compared a first version used onePatch() instead of the
different faces              model's real patch partition, and the boundary range
                              is ordered by (patch, owner, key), so face n meant
                              different faces in the two geometries. The
                              orthogonal distances disagreed by a factor of three
"the boundary minimum is      asserted from one fixture; RM-MESH-02 disproved it
always the smaller"          (0.141 mm internal against 0.189 mm boundary). The
                              two minima are over DIFFERENT face sets, so nothing
                              orders them
```

And three documentation defects of the same class as the one the review named:
the first round's fix reached the ADR but **not the public header**, which kept
every withdrawn claim including the fabricated sentence verbatim; two figures were
written as `1.41e-4 mm` for `0.141 mm`, wrong by a factor of a thousand; and the
required counts in `FREEZE.txt` were written from a stale measurement with a
`- 1` invented to make the sum close. All corrected, and each recorded where it
happened rather than quietly fixed.

### What the reviews checked and found correct

The point-level formulas, derived independently; the `kTetFaces` table's
outwardness; Decision 5's steps (ii)–(vii); the sign and orientation discipline
(no place where a sign is assumed rather than read from `owner`); the boundary
case's consistency; the skewness plane intersection; the architecture (layering,
no OCCT, no Qt, no GUI state, typed indices, units at every boundary, structured
errors); the absence of FP contraction on this toolchain; and all 32 values of the
cross-milestone quality table.

## FAILURE PATHS

Nine refusal sites, every one enumerated in the header with its real status. Five
are exercised by a test: the stamp binding in both builders, and the four
reachable value guards above. Four are argued unreachable and are retained because
the argument is about this code and could stop being true.

The point-level functions are exposed precisely so degeneracy can be tested, and
`triangleAreaVector` is driven to its zero-vector answer with three collinear and
three coincident points.

## PERSISTENCE

None. Nothing is serialized, no file format moved, and no persistent identity is
minted. A `CfdGeometry` is derived state (ADR-058), keyed to a `MeshStamp`, and
reaches neither `src/io` nor `apps` — checked, not assumed.

## DETERMINISM

Every container is ordered; nothing is keyed by a hash or an address; every sum
runs in ascending index order. Two builds of one mesh and topology are equal field
for field — including every stored vector and the whole summary with its
floating-point residuals — and that is asserted on all eight reference meshes and
not only on a hand-built fixture.

`EqualityDiscriminatesSoDeterminismMeansSomething` is the companion that makes it
mean something: two geometries of different meshes compare unequal, two of
identically shaped but separately built meshes compare unequal on the stamp alone,
and their summaries — which hold no stamp — compare equal.

Cross-preset bit-identity is **not** claimed. The reason is recorded: `magnitude`
and the quality metrics go through libm (`hypot`, `acos`), which is not required to
be identically rounded between builds.

## KNOWN LIMITATIONS

1. **`orthogonalDistance` is the worst-conditioned number this milestone
   produces**, and it is the one a diffusion coefficient divides by. It is a
   difference of two cell centroids projected on the normal — catastrophic
   cancellation, with relative error of order `eps * L / (d . n)`. On two slivers
   of extent 1 m sharing a tilted face, against the exact `h/2`, the relative
   error runs `3.8e-10` at `h/L = 1e-6` to `191 per cent` at `h/L = 1e-16`. The
   eight reference meshes are nowhere near this — the smallest internal `d . n` is
   0.141 mm on a 60 mm body — and the guard catches the cases that go
   non-positive. But "measured, never judged" means a consumer can be handed a
   number that is wholly wrong with no diagnostic. A bound on it is
   P19-VALID-001's.
2. **The non-orthogonality angle is ill-conditioned near zero** — `sqrt(2 eps)`,
   about `2.1e-8` rad — and `orthogonalityCosine` is the form a solver should use.
   Both are documented on the accessors.
3. **The quality metrics' definitions rest on one closed-form anchor each.** The
   cross-milestone agreement checks the plumbing, not the definition, because both
   computations transcribe one formula. The analytic face pair is what checks the
   definitions, and it is a single configuration.
4. **The clamp's lower bound is dead code.** `std::clamp(cosine, -1.0, 1.0)`: a
   negative cosine is unreachable by Decision 5 (vi) and no fixture produces one.
   The `-1.0` half is undemonstrated.
5. **"No max-ID-sized arrays" is inherited, not re-proved.** This value is indexed
   by the topology's compact indices throughout; the property was established by
   ADR-064.
6. **Nothing tests that a compile-fail control target builds.** That is how the
   `-Werror` breakage above stayed invisible behind a green `ctest`. The next group
   making the same mistake is caught only by a full build. Fixing it is a change to
   the shared compile-fail harness and is outside this milestone.
7. **Eight reference-mesh tests depend on P16 emitting gap-free element handles.**
   `MeshBuilder` has no explicit-handle overload for an element, so the
   rigid-transform sweep rebuilds a mesh and `REQUIRE`s that the handles come back
   identical. A future backend that leaves gaps fails loudly rather than silently
   comparing different faces, but the dependency is documented nowhere else.
8. **The zero-area witness depends on there being no FP contraction.** Measured on
   the pinned toolchain: `-O3` with this project's flags gives exactly 0 and emits
   no FMA; `-O3 -march=haswell` gives `1.3450335881681524e-17` and emits one;
   `-ffp-contract=off` restores the zero. No preset sets an FMA-capable `-march`,
   and the test branches rather than failing if one ever does.
9. **The centroid formula was not changed.** `(a+b+c+d)/4` overflows for
   coordinates near `DBL_MAX`; `a + ((b-a)+(c-a)+(d-a))/4` would not and is better
   conditioned. It was considered and rejected as out of scope: the defect was the
   false claim, not the guard, and changing the formula would shift every closure
   residual and the cross-milestone comparison after the review rounds had closed.

## REGRESSION

The full suite on all three production presets, from clean, against the frozen
tree.

```text
preset             ctest summary                    Passed markers   non-Passed
debug-ext          100% tests passed out of 6058    6058             0
release-ext        100% tests passed out of 6058    6058             0
debug-shared-ext   100% tests passed out of 6058    6058             0
```

6058 is not read as "no failures" but as an arithmetic identity: 5998
(P19-TOPO-001's qualified count) + 31 unit + 29 compile-fail, exactly, with no
remainder. A count needing a fudge term would mean a case had been dropped or
registered twice.

That a suite passes does not prove this milestone's tests were in it. The group
counts were read per preset, and they are identical across all three:

```text
unit.CfdGeometry_        31      compile_fail.cfdgeom.    29
unit.CfdTopology_        36      compile_fail.cfdtopo.    23
unit.CfdMesh_            20      compile_fail.cfdids.     28
architecture.           205
```

The four unchanged CFD groups and the 205 architecture tests, which hold the
containment and layering tables in force, are the blast-radius evidence that a
layer-0 header change moved nothing.

Build diagnostics, all three presets: 0 `warning:`, 0 `error:`, 0 `FAILED:`. The
project builds with `-Werror`, so a warning would have failed the stage anyway;
the count is recorded because H2 showed that a green `ctest` can sit over a
project that does not fully build. Each build was followed by a no-op rebuild
that found nothing to do.

## QUALIFICATION

Seventeen stages — configure, clean, build, no-op rebuild and ctest on each of
three presets, plus two repeat stages — every one exit 0, `0 stage(s) failed`.
2026-10-31 18:46:15 to 22:14:13, 3 h 28 m.

**Determinism.** `ctest --repeat until-fail:5` on release-ext and debug-ext: each
selected test had to pass five consecutive times. The filter selected 2964 on
both, matching the freeze, and the guard above the repeat loop plus the test
presets' `noTestsAction=error` means a filter matching nothing would have been a
failed stage rather than a silent pass.

```text
repeat preset      distinct tests   executions   non-Passed
release-ext        2964             14820        0
debug-ext          2964             14820        0
```

14820 = 2964 x 5, exactly, in both logs, including 155 executions of the 31 new
cases in each.

A counting trap, recorded because it would mislead the next reader: ctest prints
the `N/2964` progress prefix only on a test's **final** repetition, so counting
lines matching `N/M Test #` gives 2964 and makes the determinism gate look as
though it ran once. The right count is every `Test #N: ... Passed` marker. I made
that mistake first, which is why both numbers are in the table. A cross-log total
of 29650 rather than 29640 is also benign and explained:
`unit.StructuralValidation_RecordsTheWorstOfEveryMetricWhetherOrNotItPassed` ends
in the word "Passed", so a per-occurrence extract counts it twice per execution —
5 x 2 logs = 10.

**Toolchain, read from the qualified build trees rather than assumed.** All three
caches record the pinned WinLibs GCC 16.1.0 r4 and CMake 4.4.1. This matters: a
fresh shell on this machine resolves gcc and cmake to MSYS2 UCRT64, GCC 16.2.0
and CMake 4.4.3, and without `run-qualification.cmd`'s PATH line every count,
warning count and subtree hash would still have looked right on the wrong
toolchain. The deviation the freeze recorded — reconfigures during development
had rewritten all three caches with MSYS2 CMake 4.4.3 — is therefore resolved,
not merely noted.

**The qualified tree is the committed tree.** The harness recorded the eight
subtree identities before the first build and after the last test run; both equal
`FREEZE.txt`. They were then recomputed after the run by qualify.cmd's own method
— a scratch `GIT_INDEX_FILE`, `git read-tree HEAD`, `git add -A` over the eight
paths, then `git write-tree --prefix=` per directory — and not by
`git rev-parse HEAD:<path>`, which reads the committed tree and is what went
wrong once in P19-TOPO-001. All eight identical; all nine candidate file hashes
unchanged; HEAD still 16cd745.

**H1 did not recur.** Every clean and every build stage exited 0 on the first
attempt, so no stage was retried and nothing was accepted from a retry inside the
run.

One cosmetic defect, recorded rather than fixed: `run-qualification.out` opens
"P19-MESH-001 preflight" because `preflight-space.ps1` is a byte-identical copy
of the file that milestone settled on, and its banner is hard-coded. It reports
this run's volumes against this run's threshold; only the title is stale. Editing
it would have broken the byte-identity that makes the harness comparable across
fifty-five milestones.

## RESULT

```text
RESULT: PASS
```

Proven: 6058 tests on three presets with this milestone's groups demonstrably
among them; 2964 x 5 repeat executions on two presets with zero non-Passed; zero
build diagnostics under `-Werror`; the closure, divergence-theorem, closed-form,
cross-milestone and OCCT validations recorded under INDEPENDENT VALIDATION; and
the qualified tree identical to the tree on disk, read twice by the harness and
once independently of it.

Not proven, and not claimed: cross-preset bit-identity, for the libm reason in
DETERMINISM; a bound on `orthogonalDistance` for near-degenerate cells, which is
P19-VALID-001's work; and the nine items under KNOWN LIMITATIONS. The `-1.0` half
of the cosine clamp remains undemonstrated dead code.

## REVISION

First and only revision. Two review rounds ran before the freeze; the second found
that the first's fixes had not reached the public header, and found the fourth
reachable guard. The freeze was taken after all of it, with two identical readings
of the eight tree identities.
