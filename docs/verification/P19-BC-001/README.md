# P19-BC-001 — CFD Boundary Conditions

```text
TASK:            P19-BC-001 — CFD Boundary Conditions
AUTHORIZED:      2026-11-05 by the owner ("implement and fully verify", with the
                 twenty-item checklist and the canonical/derived split quoted).
IMPLEMENTATION:  cfd::TargetProblem and the three-check face resolver; the
                 frozen per-field treatment table; the prepared boundary with
                 its per-face prescribed values, its three-count cardinality
                 record and its source binding; the pressure datum, including a
                 point locator. ADR-067, nine decisions.
TESTS:           48 unit cases / 2136 assertions, 16 compile-fail gates.
VALIDATION:      P16's reverse linkage against its forward resolution; the
                 hydrostatic formula against centroids recomputed from the mesh;
                 the barycentric containment test written out again; the datum
                 rule against P19-DATA-001's own pressureDatumProblem; an
                 inlet's direction against the owner cell's interior.
RESULT:          PASS. 17 of 17 qualification stages exit 0; 6184 tests on
                 each of three presets, 3039 x 5 twice over, 0 failures.
                 The commit and the fifth tree reading are in REVISION r2.
EVIDENCE:        this directory. FREEZE.txt records the tree that was qualified.
```

## SCOPE

The twenty checklist items of `TODO.md`, and nothing else. What is NOT built,
each for a stated reason:

```text
no matrix, no source vector, no operator, no solve
    src/cfd/CMakeLists.txt records that this module links no linear algebra and
    that admitting a matrix is P19-LINSOLVE-001's decision. The TODO's Derived
    block names "matrix/source contributions"; those are derived by a LATER
    milestone, and this one produces what an operator will read.
no flux-balance check
    ADR-058 refuses incompatible prescribed fluxes and P19-MASSBAL-001 fixes the
    numbers. Not on this checklist.
no backflow sign claim
    CfdData.hpp:190 left the policy here, and the policy is that a pressure
    outlet does not constrain the sign of phi: backflow is admitted, never
    clamped. That is the same statement as `Computed`.
no turbulence, temperature or species boundary condition
    There is no field for them (ADR-058; ADR-066's closed variable table).
```

## BASELINE

```text
9e76814  the documentation fix that preceded this work: P19-DOF-001's summary in
         TODO.md and ROADMAP.md named a type, BoundaryFieldSlot, that exists in
         no header. Found while reading the field layout in order to build on
         it -- a name that appears in no code cannot fail a build. The evidence
         README and ADR-066 both named the real type (BoundaryFaceIndex)
         correctly, so the documents were wrong and were corrected.
c069035  P19-DOF-001 PASS, committed and pushed. 6120 tests per preset.
```

The canonical boundary-condition vocabulary already existed. `CfdData.hpp`
defines all six kinds `ADR-058` authorizes — velocity inlet, static-pressure
inlet and outlet, no-slip wall, moving wall, slip/symmetry — each naming a CAD
face by `FaceName` and holding typed physical values and nothing else about a
mesh, and `CfdAnalysisDefinition` already owns them in the user's order. So the
first five checklist items were not a request to declare those records; they
were a request to QUALIFY them against a mesh, which is what `ADR-058` means by
"P19-BC-001 qualifies each; a condition it cannot qualify is refused, not
approximated".

Four earlier milestones had already written down what this one owed, and those
sentences were treated as the requirement rather than as commentary:

```text
CfdAnalysisObject.hpp:100  the gate that refuses unassigned boundary facets is
                           the one that refuses a missing pressure datum
CfdAnalysisObject.cpp:150  "P19-BC-001 refuses a facet that two conditions claim"
CfdData.hpp:157            every boundary facet carries exactly ONE condition; a
                           velocity points INTO the domain; a moving wall's
                           velocity is TANGENTIAL
CfdData.hpp:190            backflow at a pressure outlet is this milestone's
                           policy to state, not a default
CfdData.hpp:345            whether the reference point lies in the fluid is
                           answered where the datum is resolved
CfdData.hpp:370            a prescribed boundary pressure varies hydrostatically
                           under gravity. "P19-BC-001 applies it."
CfdMesh.hpp:222            an unnamed patch can carry no condition, so this
                           milestone refuses an analysis in which one exists
ADR-063 Decision 3         one name may cover several patches, and one patch may
                           carry several names. NO NEAREST-FACE FALLBACK.
```

## ARCHITECTURE

`ADR-067`, nine decisions. The ones that were not obvious:

```text
D1  A prepared boundary is a derived value with a private constructor and one
    assembler -- the sixth use of that device in this codebase. The gate is
    staged after the mesh contract, the topology and the geometry, and takes
    all three as arguments rather than building them, so a caller holding one
    for a DIFFERENT mesh is refused rather than silently rebuilt.
D2  FOUR policy values, not the checklist's two. A slip wall's velocity is
    neither fixed nor zero-gradient (Symmetry), and phi at a pressure boundary
    is an outcome (Computed). Two values would have forced every operator to
    re-derive the treatment from the condition's kind, and the first that forgot
    would admit through-flow at a symmetry plane -- a wrong answer that still
    converges.
D4  A THIRD copy of the three-check face resolver, because cfd is layer 42 and
    may include neither thermal (45) nor structural (50). ThermalTarget.hpp's
    own header advises writing it before a second consumer needs it and records
    what deferring cost P17: a qualified production file had to be requalified.
D5  Orientation is verified PER TARGET, and a non-planar moving-wall target is
    refused by name. The first draft tested tangency per facet; see ADVERSARIAL
    REVIEW.
D6  Cardinality carries THREE counts, because the first two cannot disagree.
D7  Both datum arms are refused here, and the datum records are validated here,
    because this gate reads them and takes a plain definition.
D8  The density is carried in the source binding only when it entered a value --
    gravity present AND a boundary fixing the pressure -- so a material edit
    cannot stale a gravity-free analysis.
```

## BLAST RADIUS

New files only, plus four registration points. No qualified production file's
behaviour changed.

```text
new       include/bettercad/cfd/CfdTarget.hpp     src/cfd/CfdTarget.cpp
          include/bettercad/cfd/CfdBoundary.hpp   src/cfd/CfdBoundary.cpp
          tests/cfd/CfdBoundaryTests.cpp          (hand-built seam)
          tests/cfd/CfdBoundaryMappedTests.cpp    (authority, real meshes)
          tests/compile_fail/CfdBoundaryMisuse.cpp
modified  src/cfd/CMakeLists.txt             two sources registered
          tests/CMakeLists.txt               two test files registered
          tests/compile_fail/CMakeLists.txt  the cfdbc group, 16 cases
          tests/cli/ZeroMatchGuard.cmake     two filters and 48 names
docs      docs/architecture/decisions/ADR-067-*.md
          TODO.md, ROADMAP.md, this directory
```

Three of the eight frozen subtrees moved — `include`, `src`, `tests` — and the
other five are byte-identical to the values `P19-DOF-001` froze. That is a
cross-check on the freeze method as much as on the tree.

Direct callers of the new API: none yet, by design. `P19-VALID-001` will read a
`PreparedCfdBoundary`; the operator milestones will read `treatmentOf` and the
per-face values. Nothing serialized changes: a prepared boundary is never
persisted and never a member of anything canonical, and a compile-fail case
holds that rather than a comment claiming it.

## TESTS

```text
unit.CfdBoundary_          48 cases
compile_fail.cfdbc.        16 cases
```

Two test files, and the split is the point. `CfdBoundaryTests.cpp` drives
`bindCfdBoundary`, which takes a plain topology, a plain geometry, the mesh and
already-resolved targets — so a facet the topology does not hold, a boundary
face no condition claims, two conditions on one facet and a target whose facet
list repeats can all be PRESENTED rather than hoped for.
`CfdBoundaryMappedTests.cpp` drives the same pass through P16's own resolution
on real meshes, and is the authority for everything about naming and
attribution. Neither replaces the other: a document cannot be asked to produce
an uncovered facet, and a hand-built target cannot prove a `FaceName` resolves.

`bindCfdBoundaryFailure` exists because a message is not a tag. The seam returns
only an `Error`, so without it a test could assert what a refusal SAYS and never
which problem it IS — and a review found eight refusals whose tag could have
been swapped for another with every message assertion still passing.

## INDEPENDENT VALIDATION

Five oracles, none of which shares a code path with what it checks:

```text
P16's REVERSE linkage          `attributed`, the count of a condition's assigned
                               boundary faces whose CAD face carries that
                               condition's own FaceName, read through
                               `sourceFaceOf` -- the other direction of the map
                               from the `boundaryFacetsOf` that produced the
                               forward count. Asserted equal on every reference
                               mesh.
centroids from the MESH        the hydrostatic prescribed pressure compared
                               against `p_b - rho |g| (h - h_ref)` with the
                               facet centroid recomputed from the mesh's own
                               nodes, not read from CfdGeometry, which is what
                               the production path used.
the barycentric sign test      `containsPoint`, written out again in the test
                               file, used to prove that BOTH cells of a shared
                               face contain the point -- so the locator's answer
                               is a CHOICE between two valid cells and the
                               lowest-index rule is what is being asserted.
pressureDatumProblem           P19-DATA-001's own implementation of ADR-058's
                               datum rule, compared against this gate's on all
                               FOUR combinations of (a boundary fixes the
                               pressure) x (a reference pressure is given).
the owner cell's interior      an inlet's prescribed velocity dotted with
                               `cellCentroid(owner) - faceCentroid`, which is
                               two centroids and no normal. If
                               `boundaryOutwardNormal` were reversed, the
                               velocity would blow fluid out of the domain and
                               only this assertion would notice.
```

Analytic closed forms where they exist: the height along gravity (`h = 5 m` for
a point 5 m up, `h = x` when gravity points along `-X`), the hydrostatic drop
(`rho g` per metre, and `1000 x 1.62` under lunar gravity so hardcoding 9.81
would fail), a unit tetrahedron's centroid, and the four barycentric
sub-volumes.

All eight meshable P16 reference bodies are swept, and the outcome must be one
of three with a reason that is itself checked. Measured: all eight come out
FULLY COVERED, 0 refused for an unnamed patch, 0 for an overlap. The sweep
asserts those three numbers exactly rather than `covered > 0`, so a body that
later lost a name would fail instead of quietly testing less.

## ADVERSARIAL REVIEW

Two independent reviews ran on the candidate, plus the author's own pass. They
found thirteen defects. Nine changed behaviour and all nine are fixed; the other
four were evidence defects and are also fixed. Six of the fixes are verified by
mutation — the mutation is applied, the suite is seen to fail, and the fix is
restored.

```text
FIXED, BEHAVIOUR
1 The declared check order was not the implemented order. `stage()` resolved
  every target before the conditions were validated, so a definition with both
  a malformed condition and an unresolvable target reported the target while
  the header claimed otherwise. Conditions now run first (thermal's order, for
  thermal's reason). MUTATION-VERIFIED: restoring the old order fails 2 cases.
2 `TargetInvalid` was a dead enumerator, reachable only BECAUSE of that
  disorder: `validate(condition)` applies core's own `validate(FaceSelector)`,
  the same check `boundaryFacetsOf` applies. Removed; a malformed selector is a
  ConditionInvalid carrying core's message, and a test proves the subsumption
  while keeping `TargetProblem::SelectorInvalid` covered where it IS reachable.
3 The moving-wall tangency check refused physically valid input. The tolerance
  was 1e-9 per facet, justified by "a meshed facet's normal carries geometric
  approximation" -- a reason that refutes that number: on a tet-meshed cylinder
  the axial component of a facet normal is of the order of the tessellation
  angle (~9e-2 at 0.3 rad), so the one constant velocity that IS tangential to
  a cylinder would have been refused, with a diagnostic saying the velocity is
  not tangential when it is. Now the target must be PLANAR -- refused by name
  if not, which is ADR-058's rule for what cannot be qualified -- and tangency
  is decided against its one normal. MUTATION-VERIFIED.
4 The gate accepted a definition `validate(definition)` refuses. An outlet plus
  a reference pressure is Overdetermined; the gate succeeded and located the
  point, contradicting the header's own claim about when `referenceCell()` is
  present, and a consumer applying both datums would have double-fixed the
  level. Both arms are now refused.
5 The datum records were never validated on this path, so a zero gravity vector
  -- which `validate(CfdGravity)` refuses, because no gravity is the ABSENCE of
  the record -- reached the arithmetic and was reported as a non-finite product,
  naming the wrong cause. `DatumInvalid` and `HydrostaticDensityMissing` added.
6 A repeated facet in one target was reported as "claimed by both 3 and 3".
  `TargetFacetsNotUnique` added; the list must be strictly ascending, which is
  what P16 delivers and what thermal refuses the same shape for.
7 One resolution per target instead of two, and no `.error()` read off a
  `Result` that had not been checked -- correct only because the two calls were
  pure functions of one input, which is an invariant across two calls rather
  than a guarantee.
8 The values loop indexed `targets[claimant[b]]` on a precondition established
  twenty lines earlier, so it was undefined behaviour rather than a diagnostic
  if that ever changed. Found by mutation 4, which crashed instead of failing.
  Both loops are now total.
9 The seam discarded the problem tag. `bindCfdBoundaryFailure` added.

FIXED, EVIDENCE
10 The inlet orientation assertion was a tautology: it dotted the prescribed
   velocity with the normal it had been constructed from, so it held
   algebraically. Now checked against the owner cell's interior.
11 The cardinality record could not disagree with itself: `assigned` was
   counted off the same claim array `expected` filled, and a repeated facet is
   refused, so the two were identically equal on every success. `attributed`
   added -- the reverse reading, which can differ.
12 The hydrostatic spread guard measured distance from zero: both extremes were
   seeded to 0.0 and every pressure in the fixture was negative, so `highest`
   never moved and a uniform field would have passed. Seeded from the data, and
   the bound now comes from the measured centroid heights.
13 A mapped fixture picked its wall by `listFaces` order while reasoning about
   which plane that face lay in -- an order `GeometryMeshMap` says carries no
   meaning. The wall is now identified from the mesh.

ALSO CORRECTED: one vacuous assertion of the author's own, `CHECK(std::abs(x)
>= 0.0)`, found by mutation analysis rather than by either review.

NOT FIXED, AND WHY
  `DegenerateFacet` is unreachable; see KNOWN LIMITATIONS 1. It is recorded as
  unreachable rather than claimed tested.
```

## FAILURE PATHS

Twenty refusals, in the declaration order, which is the phase order: the two
bindings, the mesh's naming, each condition on its own terms, resolution,
coverage, the per-facet values, the datum. Within the per-facet pass the first
offending facet in `BoundaryFaceIndex` order decides, which is stated in the
header rather than implied, because claiming an order the code does not have
would be a false claim about a diagnostic.

Nineteen of the twenty are reached by a test and asserted BY TAG, not merely by
message. The twentieth is `DegenerateFacet`.

Atomic: one refusal refuses the whole set, and the prepared value is assembled
only after every check has passed. A failed preparation publishes nothing.

## PERSISTENCE

Nothing in this milestone is persisted, and that is the claim rather than an
omission. A `PreparedCfdBoundary` is derived, bound to one mesh generation and
one list of conditions, rebuilt after a remesh or any condition edit, and never
a member of anything canonical. Two compile-fail cases hold that the canonical
definition has no member for a prepared boundary and none for a resolved facet
set, and two more hold that a canonical condition carries no facet handle and no
boundary-face index. So the checklist's "No mesh face IDs in canonical BC
intent" is a build-time property, not a review note.

## DETERMINISM

No unordered container, no parallelism, no static state, no hashing, no address
or pointer value, no wall-clock time, no locale and no random seed anywhere in
the new code. Conditions are processed in the definition's canonical order;
facets arrive from P16 ascending and deduplicated, and the code refuses a list
that is not; boundary faces are walked in `BoundaryFaceIndex` order.

The point locator returns the LOWEST `CellIndex` that contains the point, so a
point on a shared face, edge or vertex resolves to one cell and the same cell in
every build — asserted against the minimum over the containing cells computed
by the independent oracle, not merely against a repeated call.

Repeated preparation is bit-identical, and equality is shown to DISCRIMINATE in
both files, so the comparison is not vacuous. Diagnostics do not depend on the
order the conditions were listed in.

## REGRESSION

```text
THREE-PRESET QUALIFICATION: PASS -- on qualify.cmd's own verdict, which is the
one that counts. P19-DOF-001's attempt 1 exited 0 at the LAUNCHER while two of
its stages had failed, so a launcher's exit code is not evidence of anything,
and the background job's reported "exit code 0" was not read as a result here
either:

    "Qualification passed: every stage exited 0."
    "qualification finished Thu 11/05/2026 16:22:18.29, 0 stage(s) failed"

17 of 17 stages exit 0 -- clean, configure, build, no-op rebuild and ctest for
each of three presets, then the two repeat stages. 12:53:46 to 16:22:18,
3 h 28 min 32 s, qualified from clean on the FIRST attempt.

    stage                tests  ctest verdict                     wall
    debug-ext             6184  100% tests passed out of 6184  2143.17 s
    release-ext           6184  100% tests passed out of 6184  2084.13 s
    debug-shared-ext      6184  100% tests passed out of 6184  2242.89 s
    repeat release-ext    3039  100% tests passed out of 3039  1089.58 s
    repeat debug-ext      3039  100% tests passed out of 3039  1223.98 s

COUNTS CHECKED AGAINST SUMS WRITTEN DOWN BEFORE THE RUN, in FREEZE.txt, not by
scanning the logs for the absence of the word "fail". P19-DOF-001's attempt 1 is
why that distinction is not pedantry: its repeat stage ran 352 tests instead of
2991 and every one of them passed, so an absence check would have called it
clean.

    6184 completion lines on each preset, and 6184 of them say Passed
    6184 = 6120 (P19-DOF-001's qualified total) + 48 new unit.CfdBoundary_
           + 16 new compile_fail.cfdbc.
    3039 selected by the repeat filter on BOTH presets, as announced by the
         harness itself before each stage ("repeat ... selects 3039 test(s)")
    15195 Passed markers per repeat stage = 3039 x 5, exactly
    0 ***Failed, Timeout, Exception, Not Run or Skipped markers anywhere
    270 lines on stderr, every one a git CRLF warning, nothing else
    0 lines containing "warning" in any of the three build logs. -Werror is on
      -- BETTERCAD_WARNINGS_AS_ERRORS:BOOL=ON, read out of the qualified tree's
      own CMakeCache.txt rather than out of CMakePresets.json -- so a warning
      would have failed the build; the count is recorded rather than inferred
      from the exit code.

THIS MILESTONE'S OWN TESTS: 48 unit.CfdBoundary_ Passed on each of the three
presets, 240 = 48 x 5 in each repeat log, and 16 compile_fail.cfdbc. Passed on
each preset. The compile_fail set is a BUILD-time gate -- proof that the misuse
does not compile, not merely that a test ran and returned true.

THE SAME ASSERTIONS IN ALL THREE BUILDS, which is a stronger statement than
three passes. The qualified binaries were re-run by name after the run, the
source tree untouched:

    debug-ext          2136 assertions in 48 test cases
    release-ext        2136 assertions in 48 test cases
    debug-shared-ext   2136 assertions in 48 test cases

A count that agreed only in Debug would mean a branch compiled out, and three
identical counts say no assertion here is configuration-dependent. The whole CFD
subsystem on the qualified debug-ext binary is 509537 assertions in 228 test
cases -- the same figure the pre-freeze measurement gave, which is a second,
independent check that the tree did not move.

cli.mesh.zero-match-guard Passed on all three presets. That is the test which
catches a ctest -R filter silently matching nothing, and this milestone added
two filters and 48 names to it, so of the existing tests it is the one most
exposed to this work.

cli.new.unicode-path, which failed my pre-freeze ad-hoc run under the OEM code
page, Passed on all three presets here (test #4909). The harness pins
chcp 65001; FREEZE.txt carries the diagnosis and the measurement that proves it,
and no source or test file was changed for it.

THE NO-OP REBUILD printed one line on each preset -- "[1/8] Checking git
revision", "[1/20]" on debug-shared-ext -- so a second build of the same tree
recompiled nothing. No generated header, stamp or discovery step is unstable.

BUILD COST, recorded because it is the one thing that moved: debug-ext 19 min
55 s, release-ext 22 min 2 s, debug-shared-ext 19 min 52 s, against
P19-DOF-001's 18.3 / 19.1 / 16.8 min at the same -j4. Two new sources and two
new test translation units account for it; no flag changed.

QUALIFIED TREE == COMMITTED TREE. The eight identities have been read FOUR times
and were identical every time: at the freeze, by qualify.cmd before the first
build, by qualify.cmd after the last test run, and from the working tree after
the run by qualify.cmd's own method. The FIFTH reading -- from the commit object
itself -- is the one that closes the gate, because the first four could all be
true of a tree that was then committed with something else in it. It is recorded
in REVISION r2.

    apps               fc398a3d2e524178cf38e8b667236379d8381086
    include            2cea8da0a007ef6790d0f3cb866d1583634cf228
    src                b928165c77bdc662e5a655645e31741735bde858
    tests              df4ae5746e4c62cbe88bb4d7f7dd7a586340da44
    examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
    cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
    CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
    CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

qualify.cmd is byte-identical at d313a64070718c44fae290ac042fe259d1a03c8b, its
57th milestone unchanged; the four gate-defining settings in
run-qualification.cmd are byte-identical to P19-DOF-001's,
CMAKE_BUILD_PARALLEL_LEVEL included; and the only harness file that differs is
that driver, in its comments alone, proved by a diff over every non-rem line.
```

## PERFORMANCE

No performance result is measured or claimed. `locatePoint` is a linear scan
over cells with a barycentric sign test, chosen because the question is asked
once per gate and an acceleration structure would be one more thing to build,
keep current and invalidate (ADR-067 Decision 7 rejected that candidate
explicitly).

## KNOWN LIMITATIONS

```text
L1 DegenerateFacet IS UNREACHABLE, and is recorded as such rather than claimed
   tested. Possession of a CfdGeometry already proves every face has a finite
   area vector and a finite centroid -- buildCfdGeometry refuses a zero-area
   face of a cell whose volume computed positive -- and that builder is the
   plainest input this milestone can be given, so no geometry with an unusable
   normal can be presented. The enumerator is kept because the alternative at
   that site is skipping the facet, which would drop it from the coverage count
   and make the coverage claim vacuous.
L2 A MOVING WALL IS PLANAR-ONLY. A curved target is refused by name. ADR-058's
   authorized uses -- a driven cavity's lid, a Couette plate -- are planar, so
   nothing in v1's scope is lost, but a sliding cylindrical wall is not
   expressible and would need a per-facet velocity field this milestone does
   not have. This is a NARROWING introduced by this milestone's review, and it
   is the owner's to revisit if a rotating-wall case is ever wanted.
L3 NO CROSS-PRESET GOLDEN DIGEST. A prepared boundary is derived and never
   persisted, so there is nothing to compare across presets beyond equality
   within one process, which the determinism cases assert. P19-DOF-001 took the
   same position for the same reason.
L4 A DELETED FACE is not exercised. The unresolved-reference cases use a
   foreign ObjectId and an entity the sketch does not have; P16's own suite
   covers a face removed by a later feature
   (MapTopologyChange_ADeletedFaceBecomesUnresolvedAndIsNeverRebound).
L5 ONE NAME COVERING SEVERAL PATCHES is permitted by ADR-063 Decision 3 and is
   not demonstrated: all eight reference bodies come out with one name per
   patch, so the sweep never enters that arm. The refusal direction -- one
   patch claimed by two conditions -- IS demonstrated, on a hand-made overlap
   and through the channel.
L6 cli.new.unicode-path depends on the invoking shell's console code page and
   passes only at 65001. Unchanged from P19-DOF-001's L6, which recommended
   passing ENCODING UTF-8 at tests/cmake/RunAndCheck.cmake's execute_process
   call; that is shared test infrastructure and outside this milestone's scope.
   FREEZE.txt records the measurement that proves it is the code page.
L7 P19-MESH-001's owner finding F1 stands unchanged: the eight P16 reference
   meshes are poor finite-volume meshes. This milestone sets no threshold and
   measures no quality.
L8 THE TWO PRE-EXISTING DUPLICATE CTEST NAMES P19-DOF-001 reported are still
   there, unfixed, as outside this milestone: MeshIssueKind_EveryKindHasAName
   in two meshing files and "Dependency cycles are reported and block their
   dependents" in two features files.
```

## RESULT

```text
PASS.

Every gate the milestone carries was run and passed, in this order: targeted
tests, independent validation against references computed outside the
implementation, the author's own adversarial pass and two independent reviews, a
full three-preset qualification with repeat stages, and a tree-identity check.

    implementation    include/bettercad/cfd/CfdBoundary.hpp, CfdTarget.hpp
                      src/cfd/CfdBoundary.cpp, CfdTarget.cpp
    tests             48 unit.CfdBoundary_ / 2136 assertions, and 16
                      compile_fail.cfdbc. build-time gates
    validation        P16's reverse attribution against its own forward
                      resolution; the hydrostatic term against centroids
                      recomputed from the mesh nodes; an independent
                      barycentric containment oracle; the datum rule against
                      P19-DATA-001's own pressureDatumProblem; six mutations,
                      each of which fails loudly
    review            13 behavioural defects found before the freeze -- 4 by
                      the author's own pass and 9 by two independent reviews,
                      which also confirmed the same 4 -- all resolved, none
                      deferred, 6 of the fixes mutation-verified
    regression        6184 tests x 3 presets, 3039 x 5 x 2 repeat, 0 failures
    determinism       the two repeat stages, 15195 markers each, 0 failures,
                      and 2136 assertions in all three builds alike
    freeze            8 subtree identities, 4 identical readings so far; the
                      fifth is read back from the commit object in REVISION r2

WHAT THIS DOES NOT CLAIM: no performance result is measured or asserted; no
matrix, source vector or solve exists, so nothing here has been shown to produce
a correct flow field, only a correct and completely attributed statement of the
boundary intent; and a prepared boundary is derived and never persisted, so
there is no cross-preset golden digest -- the three-preset evidence is that the
same assertions hold in each build, which DETERMINISM states plainly rather than
dressing up as more.

KNOWN LIMITATIONS L1-L8 stand as written, and L2 is the one worth the owner's
attention rather than a footnote: this milestone's own review NARROWED a moving
wall to planar targets, so a curved one is now refused by name. Nothing in
ADR-058's authorized scope is lost, and the narrowing is the owner's to revisit.

All twenty checklist items in TODO.md are satisfied, and are ticked in the same
commit as this evidence and the production code.
```

## REVISION

```text
r1  2026-11-05. Drafted during the work. Every section except REGRESSION, RESULT
    and this one was written before the qualification launched, and those three
    were left as placeholders rather than written in anticipation of a result.
    No number in this file is unsupported by a log in this directory or by a
    command recorded beside it.

    THE AUTHORIZATION WAS NOT RECORDED IN TODO.md BEFORE IMPLEMENTATION, and
    that is a deviation from P19-DOF-001's practice rather than a difference of
    opinion about it. That milestone's evidence records the authorization being
    written into TODO.md in six places before any code was written; here TODO.md
    still read "P19-BC-001 NOT AUTHORIZED" at its status table while the work
    was done, and the authorization and the PASS are recorded in the same edit.
    The authorization itself is not in doubt -- the owner pasted this
    milestone's whole TODO section and said "implement and fully verify" -- but
    the repository did not say so at the time, and a reader of the tree alone
    could not have told. Recorded, not tidied away.

    TWO COUNTING ERRORS OF MINE were caught while reading this run's logs, and
    both are kept, because both are the failure mode this project's evidence
    discipline exists for: a broken pattern returning a plausible number rather
    than an error.

      1 compile_fail.cfdbc. was first counted at 4 of 16. The regex
        [A-Za-z0-9_]+ stops at a hyphen and these test names are hyphenated, so
        it collapsed sixteen names onto their four distinct first words. The run
        was right and the measurement was wrong. Recounted with [a-z0-9-]+ it is
        16 on each preset, and 16 of 16 say Passed.
      2 the three -ext presets were first read as NOT inheriting
        BETTERCAD_WARNINGS_AS_ERRORS, because the script that walked the
        inheritance followed only the first parent of a list and debug-ext
        inherits ["external", "debug"]. The value was then read where it
        actually binds -- the qualified build tree's own CMakeCache.txt, ON --
        which is the better source anyway, being the tree that was built rather
        than the file that configured it.

    An earlier error of the same family is recorded in FREEZE.txt: an attempt to
    re-run one test printed "No tests were found!!!" and would have exited 0,
    which is precisely what tests/cli/ZeroMatchGuard.cmake exists to prevent,
    met by accident while verifying something else.

    ONE PRE-EXISTING CONDITION is deliberately not fixed here, being outside the
    authorized milestone, and is carried forward unchanged as L8: the two pairs
    of distinct Catch2 cases that share a ctest name, reported first by
    P19-DOF-001. All four still pass; it is naming hygiene, not correctness.
```
