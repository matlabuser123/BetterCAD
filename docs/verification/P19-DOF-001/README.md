# P19-DOF-001 — CFD Field / DOF Model

```text
TASK:            P19-DOF-001 -- the CFD field layout: where each field lives, how
                 long an array of it is, which subscript addresses one component,
                 and what each kind of edit invalidates.
AUTHORIZED:      2026-11-04 by the owner ("implement and fully verify", with the
                 seventeen item checklist and the recommended p / U / phi layout
                 quoted). Recorded in TODO.md in six places before any
                 implementation began.
ARCHITECTURE:    ADR-066.
RESULT:          PASS. 17 of 17 qualification stages exit 0; 6120 tests on each
                 of three presets, 2991 x 5 twice over, 0 failures. Committed
                 as cdd42a3, qualified tree == committed tree.
```

## SCOPE

What the checklist asked for, and nothing else. In particular NOT the result's
field arrays, which `CfdResult.hpp` had promised this milestone would add; that
deferral and its reason are ADR-066, Decision 1, and the two comments that
promised otherwise were corrected rather than quietly dropped.

```text
in scope      the three field locations and the frozen variable table with its
              dimensions; one new subscript (the component-blocked cell vector);
              the translations; the binding to a mesh generation and a topology;
              the three-valued invalidation verdict; the refusal of a layout over
              no cells
out of scope  field VALUES of any kind, boundary conditions and their resolution
              (P19-BC-001), initial conditions applied to cells (P19-IC-001),
              gradients, operators, matrices, solves, and the result's arrays
```

## BASELINE

`16cd745`..`a9cd4f6` — P19-GEOM-001 PASS, committed and pushed. 6058 tests per
preset. Nothing after it was authorized until this milestone's "implement and
fully verify".

## ARCHITECTURE

ADR-066, seven decisions. The three that were genuinely open:

```text
Decision 3   a cell-centred vector is COMPONENT-BLOCKED, comp * cells + cell,
             which REVERSES P17-DOF-001's interleaving. The reason is the
             authorized solver: ADR-058 fixes SIMPLE-family coupling, so momentum
             is three scalar systems of `cells` rows solved one at a time and the
             pressure correction is a fourth of the same shape -- four contiguous
             runs, one scalar interface. P17 interleaves because a Tet4 stiffness
             matrix couples the components of one element in a full 12x12 block;
             the segregated predictor assembles no row that couples u to v.
             Candidates compared: blocked (chosen), interleaved, no subscript.
Decision 4   ADR-064's four index types are REUSED and exactly ONE is added.
             A `CellScalarIndex` would be a second name for a CellIndex over the
             same range -- the second addressing space ADR-064 avoided.
             Candidates: reuse (chosen), a parallel family, a generic template.
Decision 5   the binding is stamp + counts + the patch partition, and the verdict
             has THREE values, because the checklist's claim is about a PARTIAL
             invalidation and "same or not" cannot express one. ADR-065's two-
             valued form cannot even detect the case.
             Candidates: stamp + counts (what ADR-065 used), + the partition
             (chosen), hold the topology.
```

## BLAST RADIUS

```text
new                include/bettercad/cfd/CfdFields.hpp, src/cfd/CfdFields.cpp,
                   tests/cfd/CfdFieldTests.cpp,
                   tests/compile_fail/CfdFieldMisuse.cpp
registration       src/cfd/CMakeLists.txt, tests/CMakeLists.txt,
                   tests/compile_fail/CMakeLists.txt, tests/cli/ZeroMatchGuard.cmake
comments only      include/bettercad/cfd/CfdTopology.hpp (a stale P19-FIELD-001
                   forward reference), include/bettercad/cfd/CfdResult.hpp and
                   tests/cfd/CfdDataTests.cpp (both promised this milestone would
                   add the result's field arrays; ADR-066, Decision 1 records why
                   it does not)
NOT touched        no layer-0 header, no existing production behaviour, no
                   persisted format, no CLI, no reference model. The milestone
                   adds a derived value and changes nothing that existed.
```

So the regression set is the whole suite on three presets -- the standing rule --
and not a neighbourhood of the edited files. Unlike P19-GEOM-001, which changed
`core/math/Vector.hpp`, this milestone touches no shared infrastructure, so the
repeat filter does not need the core math and units prefixes that freeze added.

## IMPLEMENTATION

```text
CfdFieldLocation       Cell / Face / BoundaryFace, with BoundaryFace a location
                       of its own because a boundary value exists only there and
                       the boundary range is grouped by patch
CfdVariable + table    p, U, phi frozen with their locations, component counts,
                       SI dimensions and symbols; static_asserts tie each row's
                       dimension to the catalog's and refuse a zero-component row
CellVectorIndex        the one new subscript, [0, 3 cells), blocked
CfdPatchFieldRange     a patch's contiguous run in a boundary field array
CfdFieldMapProblem     TopologyHasNoCells (reachable, tested),
                       TopologyIsOfAnotherMesh (the staged gate's)
CfdFieldCurrency       Current / BoundaryLayoutChanged / MeshChanged
CfdFieldMap            counts, translations, the patch partition, the binding.
                       O(patches), no mesh handle as a subscript, no field value
buildCfdFieldMap       the plain builder; refuses a topology of no cells
requireCfdFieldMap     the staged gate: names the control, refuses another
                       generation, carries the model's geometry revision
```

## TESTS

```text
unit.CfdFields_            27 cases, 58197 assertions
compile_fail.cfdfield.     35 cases
```

Every structural claim runs on EVERY input through one `checkLayout` helper, which
is P19-TOPO-001's lesson about claims checked on the one model chosen for them.

## INDEPENDENT VALIDATION

```text
the SI dimensions      checked against exponent sets written out BY HAND
                       (pressure M L^-1 T^-2, velocity L T^-1, mass flux M T^-1),
                       not against dimensions::pressure, which the header's own
                       static_asserts tie the table to. So the table is checked
                       against the physics and against the catalog separately,
                       and the kg/s form is discriminated from kg/(m^2 s)
the array lengths      counted off the P16 MESH: cells = mesh.tetrahedra().size(),
                       boundary = mesh.triangles().size(),
                       faces = (4 cells + boundary) / 2 by Euler bookkeeping.
                       On all eight reference meshes
the patch partition    counted off the P16 MAPPING: every patch's slot count
                       against that MappedFace's own facet list, and its CAD face
                       against MappedFace::index. The mapping is the partition's
                       only upstream source
the blocked formula    rewritten in the test as component * cells + cell, and the
                       subscript set proved a BIJECTION onto [0, 3 cells) by
                       marking every slot -- a round trip alone would pass for a
                       layout mapping two cells to one slot
the topology's answers four questions now have two answers in the tree (is this
                       face internal, which face is this slot, which slot is this
                       face, which patch holds it); each is cross-checked against
                       CfdTopology on every face of every reference mesh. This is
                       a cross-check between two implementations, NOT an
                       independent oracle, and the test comment says so
```

## ADVERSARIAL REVIEW

Two reviews ran before the freeze, independently, neither seeing the other's
report. Both found the same four evidence defects, which is the useful signal.

```text
D1  the canonical-state proof was VACUOUS. It built a local
    CfdAnalysisDefinition the document never held, took its canonical text,
    remeshed, and asked the same unmodified struct again -- a check that
    canonicalText is a pure function, which it is. ADR-066's defence of it ("a
    field added to the definition would have to change the text") was false: a
    velocities vector would have been empty both times. FIXED: the analysis is
    created in the document, the edit and the remesh happen there, and the
    definition is read back through findCfdAnalysis each time. The
    boundary-condition proof had the same defect and the same fix.
D2  the staged gate's one distinguishing line was a NO-OP on every tested path.
    requireCfdTopology already stamps the model's geometry revision onto the
    topology, so `map->geometry_ = model.geometryRevision()` was assigning a
    value that was already there, and the test's comment claimed a comparison it
    never made. FIXED: a plain-topology fixture, whose revision is invalid, now
    observes the assignment. VERIFIED BY MUTATION: deleting the line fails
    CfdFields_TheStagedGateReplacesThePlainTopologysGeometryRevision with three
    assertions; before the fix it passed the whole suite.
D3  a header cited a measurement nobody made: "a test measures the size against
    a mesh of four thousand cells". No such test; the largest reference mesh is
    about a thousand cells; and the test it pointed at compared sizeof of one
    type with itself, which cannot differ. FIXED: the header now names the two
    instruments that do carry the claim, and the tautology is deleted.
D4  a test comment described a fixture it did not build: "checked on a five-cell
    layout" over a two-cell one, where blocking and interleaving agree on the
    CELL and differ only in the component. FIXED: the discrimination moved to the
    block's six cells, where subscript 4 is cell 4's X blocked and cell 1's Y
    interleaved -- both halves differ.
```

Four coverage gaps, each now a test:

```text
G1  the per-patch SIZE branch of the boundary comparison decided no verdict
    anywhere: the patch count and the CAD faces decided every case. Now
    CfdFields_APatchSplitThatKeepsTheCountAndTheCadFacesIsStillDetected.
G2  every empty patch in the suite was TRAILING, where the repeated offset sits
    at boundaryFaces and `contains` has already refused the slot -- so
    upper_bound's justification was unexercised. Now
    CfdFields_ALeadingEmptyPatchStillAttributesEverySlotToItsOwnPatch.
    VERIFIED BY MUTATION: lower_bound in place of upper_bound fails loudly.
G3  no layout had internalFaces == 0. Now
    CfdFields_ASingleCellTopologyHasNoInternalFaceAndStillLaysOutThreeBlocks.
G4  the fluid-edit test never touched the VISCOSITY the header claims alongside
    the density. Now both, in CfdFields_AFluidEditLeavesTheLayoutCurrentAndEqual.
```

Four narrower corrections, all in the evidence layer:

```text
C1  "NO MESH HANDLE IS OFFERED ANYWHERE IN THIS FILE" was false of `mesh()`,
    which returns a MeshStamp carrying a MeshId. Narrowed to the claim that
    matters: no handle can SUBSCRIPT anything, and an identity is not a subscript.
C2  three assertions restated the implementation (boundaryFaces == faces -
    internalFaces, isEmpty == (count == 0), size(variable) == components *
    size(location)) and were deleted rather than reworded: they could not fail.
C3  `describesCells` is true under BoundaryLayoutChanged, where a FACE field is
    valid in its internal range and stale in its boundary tail. Accurate but
    readable as more; the doc now says it explicitly.
C4  an empty patch's range has an INVALID first slot, so `subspan(first.value(),
    0)` is undefined behaviour rather than an empty subspan. The choice stands
    (the alternative hands back another patch's slot) and the warning is now in
    the header where a consumer writes that arithmetic.
```

## FAILURE PATHS

```text
TopologyHasNoCells            REACHABLE, tested. An empty mesh with no patches
                              yields a BUILDABLE topology, because ADR-064's
                              inspection finds no defect in nothing -- the same
                              witness ADR-065 used. Refused here because a
                              numbering of nothing is an absent system, not a
                              degenerate one (P17-DOF-001's and P18-DOF-001's
                              words); ADR-065 accepts it because measuring
                              nothing is zero, and the asymmetry is deliberate.
TopologyIsOfAnotherMesh       REACHABLE, tested twice: a hand-built topology
                              against a prepared model, and a real remesh.
the wrap of a build failure
inside the staged gate        unreachable, labelled. A topology of no cells
                              cannot describe a CfdModel's mesh, so the stamp
                              check fires first.
three guards in patchOf       unreachable, each labelled with its invariant:
                              patchStart_.size() < 2 (a boundary face implies a
                              patch, ADR-064), above == begin() (patchStart_[0]
                              is 0 and 0 exceeds no size_t), patch >= patches
                              (contains bounds the slot below patchStart_.back()).
                              Kept because the invariants are the topology's, not
                              this file's.
the kUnnamedPatchFace
fallback                      unreachable, labelled. CfdTopology::patchFace is
                              total over [0, patches). A distinguishable sentinel
                              rather than 0, so that if it ever fired the binding
                              would notice instead of agreeing with CAD face 0.
the three count comparisons
in currency                   never decide a verdict, and no public API can make
                              them: one generation yields one set of counts, and
                              the stamp short-circuits. Recorded as corroboration.
```

## PERSISTENCE

Nothing is persisted, and that is the point. A layout is derived state (ADR-058),
is never written to a file, and is rebuilt from the topology whenever it is
wanted. What IS checked is the converse: an analysis held in a document keeps the
same canonical text and fingerprint across a remesh that changes the cell count,
so nothing mesh-sized is inside canonical state. There is no save/load round trip
here because there is nothing of this milestone's to save; ADR-066, Decision 7
argues for the form used instead.

## DETERMINISM

```text
no unordered container, no hash, no address, no clock, no locale, no seed.
The layout's order IS the topology's, which ADR-064 made deterministic; two
builds of one topology compare equal, and two topologies of one mesh generation
yield one layout.
Equality DISCRIMINATES -- on the stamp, on the partition and on the geometry
revision -- which is what makes "two builds are equal" mean something.
Catch2 prints a seed on every run. It selects nothing here: the file uses no
generator and no random source, and the 27 cases yield 58197 assertions under
--rng-seed 1 and --rng-seed 999331 alike, measured on the qualified debug-ext
binary after the run. So the seed is reported, not depended on.
In-process only: there is no golden digest of a layout across presets, because a
layout is derived and never persisted. The three-preset evidence is that the
same assertions hold in each, not that a byte-identical artefact was produced.
```

## REGRESSION

```text
THREE-PRESET QUALIFICATION: PASS -- on qualify.cmd's own verdict, which is the
one that counts. Attempt 1's LAUNCHER also exited 0 while two stages had failed,
so the launcher's exit code is not evidence of anything:

    "Qualification passed: every stage exited 0."
    "qualification finished Thu 11/05/2026  3:54:27.02, 0 stage(s) failed"

17 of 17 stages exit 0 -- clean, configure, build, no-op rebuild and ctest for
each of three presets, then the two repeat stages. 0:50:59 to 3:54:27, 3 h 3 min.

    stage                tests  ctest verdict                     wall
    debug-ext             6120  100% tests passed out of 6120  2002.02 s
    release-ext           6120  100% tests passed out of 6120  1809.60 s
    debug-shared-ext      6120  100% tests passed out of 6120  1961.47 s
    repeat release-ext    2991  100% tests passed out of 2991   935.78 s
    repeat debug-ext      2991  100% tests passed out of 2991  1025.15 s

COUNTS CHECKED AGAINST SUMS WRITTEN BEFORE THE RUN, not by scanning the logs for
the absence of the word "fail". That distinction is not pedantry: attempt 1's
repeat stage ran 352 tests instead of 2991 and every one of them passed, so an
absence check would have called it clean.

    6120 completion lines on each preset, and 6120 of them say Passed
    6120 = 6058 (P19-GEOM-001's qualified total, re-read from its own log)
           + 27 new unit.CfdField* + 35 new compile_fail.cfdfield.*
    2991 selected by the repeat filter on BOTH presets, as announced by the
         harness itself before each stage ("repeat ... selects 2991 test(s)")
    14955 Passed markers per repeat stage = 2991 x 5, exactly
    0 ***Failed, Timeout, Exception, Not Run or Skipped markers anywhere
    270 lines on stderr, every one a git CRLF warning, nothing else

THIS MILESTONE'S OWN TESTS: 27 unit.CfdField* Passed on each of the three
presets, and 35 compile_fail.cfdfield.* Passed. The compile_fail set is a
BUILD-time gate -- it is proof that the misuse does not compile, not merely that
a test ran and returned true.

cli.new.unicode-path, which failed my pre-freeze run under the OEM code page,
Passed on all three presets here (test #4861). The harness pins chcp 65001; the
diagnosis is in FREEZE.txt, and no source or test file was changed for it.

QUALIFIED TREE == COMMITTED TREE. The eight identities were read FIVE times and
were identical every time: at the freeze, by qualify.cmd before the first build,
by qualify.cmd after the last test, from the working tree after the run, and
read back from the commit object of cdd42a3 -- the commit that carries this evidence
and the production code together. The fifth reading is the one that closes the
gate, because the first four could all be true of a tree that was then committed
with something else in it.

    apps               fc398a3d2e524178cf38e8b667236379d8381086
    include            d6ba794a75efd1786cecdf5c620f48d356e8d93f
    src                143a993a8e0b6223cd395a963e4c07d115aa8db1
    tests              6d81bbc1b5bdc4c745f3e5a2d17c7af5f4d44efa
    examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
    cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
    CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
    CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

ATTEMPT 1 FAILED, AND IS PRESERVED RATHER THAN OVERWRITTEN, under
qualification/attempt-1/ with its logs, its times file and the driver as it ran.
FREEZE.txt carries the diagnosis: src/features/SolidSupport.cpp exited 1 with no
diagnostic at all, having left an orphaned 0-byte %TEMP%\cc*.s behind, because
the machine was out of COMMIT -- 71.3 GB used of an 80.8 GB limit before the
build started, against about 29 GB wanted at 16 concurrent compiles. The build
failure took the ctest stage with it and then the repeat stage, which selected
352 tests instead of 2991 because Catch2 discovery runs at BUILD time.

Exactly ONE line differs between the two attempts, in this milestone's own
driver: CMAKE_BUILD_PARALLEL_LEVEL 16 -> 4, with the measurements recorded in
place. qualify.cmd is byte-identical at
d313a64070718c44fae290ac042fe259d1a03c8b, its 56th milestone unchanged; all four
gate-defining set lines are byte-identical to P19-GEOM-001's; and no file in the
frozen surface was touched. Build parallelism is a RESOURCE parameter, not a
gate -- ninja compiles the same sources with the same flags and -Werror at any
-j -- so attempt 2 qualifies the same tree, and the four matching identities say
so independently.

ITS COST, measured rather than waved away: the debug-ext build went from 8.9 min
at -j16 (P19-GEOM-001) to 18.3 min at -j4, release-ext 12.1 -> 19.1, and
debug-shared-ext 11.5 -> 16.8. Ninja's edge count rose only 786 -> 789 between
the two milestones, so that is the parallelism, not extra work.
```

## PERFORMANCE

No performance claim is made and none is measured. ADR-066's consequence about
cache lines is an expectation, labelled as such, and no milestone may cite it as
measured.

## KNOWN LIMITATIONS

```text
L1  a re-attribution of patches that preserves every patch's SIZE and CAD FACE is
    not detected, so a boundary field would be reported Current while its slots
    held other faces. Not reachable from a CfdModel -- two topologies of one
    model read one mapping and are bit-identical -- so it needs hand-built
    MappedFace spans. ADR-066, Decision 5 records it, and
    CfdFields_TheOneReattributionTheBindingCannotSeeIsTheOneAdr066Records
    measures it. That test will FAIL if the binding is ever strengthened, which
    is the intended notice.
L2  an empty patch's range carries an invalid first slot, so span arithmetic on
    it is undefined behaviour unless the consumer asks isEmpty(). The hazard
    lands on P19-BC-001; the header warns where the arithmetic is written.
L3  the three count comparisons in `currency` cannot be exercised through any
    public API (see FAILURE PATHS).
L4  the static_assert bounding 3 * cells is a 64-bit assumption: on a 32-bit
    size_t the header does not compile. That is the intended loud failure, and it
    is stated in the header rather than left implicit.
L5  P19-MESH-001's owner finding F1 stands unchanged: the eight P16 reference
    meshes are poor finite-volume meshes. This milestone sets no threshold and
    measures no quality.
L6  cli.new.unicode-path depends on the invoking shell's console code page, and
    passes only at 65001. tests/cmake/RunAndCheck.cmake:40 calls
    execute_process(... OUTPUT_VARIABLE) with no ENCODING argument, so CMake
    decodes the child's stdout through the active code page and the CLI's UTF-8
    is mangled before the regex sees it. qualify.cmd already pins chcp 65001, so
    every qualification is unaffected; an ad-hoc ctest from an OEM shell is not.
    RECOMMENDED, AND DELIBERATELY NOT DONE HERE: pass ENCODING UTF-8 at that
    call site and the test stops depending on the environment. That is shared
    test infrastructure, outside this milestone's authorized scope, and the
    dependence is deliberate per tests/CMakeLists.txt:1829. Not in this commit.
```

## MEASUREMENTS THAT CORRECTED AN ASSUMPTION

```text
the planar block cannot be refined   MEASURED on RM-MESH-01 (120 x 70 x 35 mm):
                                     6 tets / 8 nodes at a 20, 12 and 4 mm global
                                     target, with and without a 0.05 mm linear
                                     deflection. Its surface triangulation has
                                     only the eight corner nodes. Two tests had
                                     assumed a remesh of it would change the cell
                                     count; one now asserts the OPPOSITE, which
                                     is the stronger claim (identical counts, new
                                     generation, verdict still MeshChanged), and
                                     the other moved to the cylinder.
the cylinder responds                MEASURED: 508 cells at its 12 mm reference
                                     intent; 786 at 20 mm, 825 at 12 mm and 2022
                                     at 8 mm with the surface controls replaced.
                                     8 mm is what the canonical-state test uses.
the block has as many patches
as cells                             6 CAD faces, 6 tetrahedra. An assertion that
                                     patches < cells failed on it; the O(patches)
                                     claim is now carried by the mirror
                                     static_assert and asserted on the tube.
P19-GEOM-001's hazard H1 EXPLAINED   that freeze carried an unexplained transient
                                     link failure of the 1.5 GB debug test
                                     executable. The same failure occurred here
                                     and the diagnostic was captured:
                                     "cannot open output file
                                     bin\bettercad_tests.exe: Permission denied"
                                     -- a concurrent run of that executable held
                                     it open. Not a toolchain fault and not
                                     transient: a lock. H1 can be closed.
the pre-freeze regression's one
failure was MINE, not the tree's     the ad-hoc debug-ext full run reported
                                     6119/6120 with cli.new.unicode-path failed.
                                     Diagnosed rather than written off, because
                                     H1 shows what writing one off costs. Same
                                     binary, same tree, one variable apart:
                                     code page 437 -> ***Failed, code page
                                     65001 -> Passed. The captured stdout shows
                                     UTF-8 bytes decoded as OEM. qualify.cmd
                                     line 39 already pins chcp 65001 and line 12
                                     says why, so the harness was never exposed;
                                     my background shell was. Checked in one
                                     direction only is not enough, so: exactly
                                     THREE lines of tests/CMakeLists.txt hold a
                                     non-ASCII byte (488-490) and all three are
                                     this one test, every other expectation is
                                     ASCII, and all 6119 of those passed under
                                     OEM -- so none can flip the other way.
                                     Targeted re-run of all 177 cli.* tests at
                                     65001: 177/177 PASS. NO source or test file
                                     was changed to resolve it.
```

## RESULT

```text
PASS.

Every gate the milestone carries was run and passed, in this order: targeted
tests, independent hand-computed validation, two independent adversarial
reviews, a full three-preset qualification with repeat stages, and a
tree-identity check.

    implementation    include/bettercad/cfd/CfdFields.hpp, src/cfd/CfdFields.cpp
    tests             27 unit.CfdField*, 35 compile_fail.cfdfield.*
    validation        hand-computed against the layout's arithmetic, independent
                      of the implementation; two mutation checks that fail loudly
    review            4 evidence defects, 4 coverage gaps, 4 corrections -- all
                      found before the freeze, all resolved, none deferred
    regression        6120 tests x 3 presets, 2991 x 5 x 2 repeat, 0 failures
    determinism       the two repeat stages, 14955 markers each, 0 failures
    freeze            8 subtree identities, 5 identical readings, the last
                      read back from commit cdd42a3 itself

What this does NOT claim: no performance result is measured or asserted, no
field VALUES exist yet, and the layout is derived and never persisted, so there
is no cross-preset golden digest -- the three-preset evidence is that the same
assertions hold in each build, which DETERMINISM states plainly rather than
dressing up as more.

All seventeen checklist items in TODO.md are satisfied, and are ticked in the
same commit as this evidence and the production code.
```

## REVISION

```text
r1  2026-11-05. First and only revision. The sections above were drafted during
    the work; REGRESSION, RESULT and this section were the only three left as
    placeholders, and were filled from attempt 2's logs after it finished. No
    section of this file was written in anticipation of a result, and no number
    in it is unsupported by a log in this directory.

    Two things in this record correct something asserted earlier in the session
    and are kept rather than tidied away -- MEASUREMENTS THAT CORRECTED AN
    ASSUMPTION holds them, including the report that attempt 1 built at ninja's
    default of about 34 jobs when the driver had in fact already pinned 16. The
    commit arithmetic still explains the failure at 16; the figure 34 was wrong.

    One PRE-EXISTING condition was found while checking this run's counts and is
    deliberately NOT fixed here, being outside the authorized milestone: two
    pairs of distinct Catch2 test cases share a ctest name --
    MeshIssueKind_EveryKindHasAName in tests/meshing/MeshDataTests.cpp:904 and
    tests/meshing/TetValidationTests.cpp:215, and "Dependency cycles are
    reported and block their dependents" in
    tests/features/RegeneratorTests.cpp:245 and
    tests/features/ValidationTests.cpp:154. P19-GEOM-001's log carries the same
    two pairs, so this milestone did not introduce them. Consequence: ctest -R on
    either name selects two tests, and a failure report does not say which source
    file it came from. Recommended as its own small task; it is a naming-hygiene
    defect, not a correctness one, and all four tests pass.
```
