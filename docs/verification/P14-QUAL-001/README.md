# P14-QUAL-001 — Full P14 Qualification

```text
FINAL STATUS: PASS
MILESTONE:    P14-QUAL-001
DATE:         2026-09-26
FINAL HEAD:   0992aec + the two qualification-gap tests (committed by this milestone)
FINAL TREE:   see FINAL TREE EQUALITY
```

## WHICH RUN COUNTS, AND WHY THE OTHERS DO NOT

Four qualification attempts appear in this milestone's history. **Only the
last one qualifies the committed tree.** Presenting them as equivalent would be
the exact dishonesty this gate exists to prevent, so they are separated here
before anything else is claimed.

```text
run  when         tests  source fingerprint      status
0    25/09 late      —   56f5a34                 BLOCKED at the precondition.
                                                 No tree freeze, NO BUILD AT
                                                 ALL. It is an audit, and it
                                                 found the blocker.
1    26/09 05:26  2256   tests 454eb29c          all stages 0 -- and
                                                 INVALIDATED: the physical
                                                 scale gate had no test, and
                                                 adding one changed tests/.
2    26/09 ~07:2x 2257   tests (intermediate)    all stages 0 -- and
                                                 INVALIDATED again: the
                                                 unwritable-destination
                                                 control had no test either.
3    26/09 08:56  2258   tests c22084fd          *** THE FINAL RUN. This is
                                                 the one that qualifies, and
                                                 the only one cited below. ***
```

Runs 1 and 2 were green. That is not the point: each was green about a tree
that is not the committed tree, because a test added after it changed a
fingerprinted path. A qualification is about one tree or it is about nothing.
Their logs are not kept, because keeping green logs that do not apply invites
exactly the confusion this section exists to remove; the run-0 audit IS kept,
below, because it is the record that found the blocker.

## PRECONDITIONS

```text
P14-STREF-001 genuinely complete and qualified      YES, at 0992aec (ADR-024)
all preceding P14 milestones complete               YES, all 17
working tree holds the intended final P14 state     YES
```

Run 0 was blocked precisely here: `P14-STREF-001`'s gate "no silent rebinding"
was NOT met. It is met now, and the closure is its own evidence — see
[P14-STREF-001](../P14-STREF-001/README.md), section CLOSURE.

## BASELINE

```text
branch        main
HEAD          0992aec + the two qualification-gap tests (committed by this milestone)
origin/main   0992aec2f3a1d4dd2235b7026d31c2e94a394ca4 (before this closeout)
HEAD^{tree}   see FINAL TREE EQUALITY
working tree  clean
git diff --check   no whitespace errors in source; the generated qualification
                   logs carry trailing spaces from cmd's `echo`, exactly as
                   every earlier milestone's committed logs do

cmake 4.4.2      ninja 1.13.2      ctest 4.4.2      git 2.55.0.windows.5
g++ 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, r4), target x86_64-w64-mingw32
generator Ninja, binaryDir build/<preset> under the source directory
Windows-11-10.0.26200-SP0, AMD64 Family 25 Model 80 (AuthenticAMD)
```

## TODO AUDIT

Counted from the final `TODO.md`, not from run 0's count.

```text
P14-ARCH-001     [x] 11   P14-BOM-001      [x] 14   P14-CLI-001     [x] 15
P14-SHEET-001    [x] 16   P14-STREF-001    [x] 14   P14-EXPORT-001  [x] 15
P14-VIEW-001     [x] 14   P14-REGEN-001    [x] 13   P14-REFMOD-001  [x] 17
P14-VIEW-002     [x] 12   P14-CMD-001      [x] 14
P14-HLR-001      [x] 12   P14-PERSIST-001  [x] 15
P14-DIM-001      [x] 15   P14-TOL-001      [x] 12
P14-ANNO-001     [x] 13   P14-ASM-001      [x] 11
```

**All 17 predecessors complete, 0 open items between them.** `P14-STREF-001`
is 14 of 14 and now headed `DONE —`; at run 0 it was 13 of 14.

`P14-QUAL-001` itself was **0 of 22 ticked while this audit ran**, and its
boxes were filled only after the final run passed — not in advance.

## MILESTONE EVIDENCE AUDIT

Every `docs/verification/P14-*/` directory exists, carries a `README.md`, has
no unfilled placeholder, and **its header status agrees with its own final
RESULT**.

```text
ARCH SHEET VIEW-001 VIEW-002 HLR DIM ANNO TOL ASM BOM
STREF REGEN CMD PERSIST CLI EXPORT REFMOD        17 / 17 agree
```

Run 0 found two disagreements; both are fixed and neither was papered over:

- `P14-REFMOD-001` read `STATUS: PENDING QUALIFICATION` while its own RESULT
  read PASS — a document asserting two states about itself. Corrected to match
  the result and the logs beside it.
- `P14-STREF-001` read `BLOCKED on one gate`, which was true then. It now reads
  PASS with a pointer to CLOSURE, and **everything above CLOSURE is left exactly
  as written**: it is the record that found the defect.

## ADR AUDIT

Fifteen P14 ADRs, `ADR-010`–`ADR-024`, from the repository rather than from the
brief's list. **No ADR is left NOT REACHED**, which is what run 0 had to do.

```text
ADR      contract                          production evidence            test evidence                                            
010  drawings live in the Document     drawing/ objects carry ObjectId  Sheet/View/Annotation document-object tests          PASS
011  intent canonical, curves derived  projectedGeometry() derives      View_TheFileHoldsIntentAndNoProjectedGeometry        PASS
012  references name semantic geometry FaceSelector has no kernel index Reference_NoPersistedReferenceCarriesAnIndex...      PASS
                                       ChamferEdgeId replaced position  Reference_NoChamferReferenceIsStoredAsAPosition...
013  viewer-facing frame, exact ratio  DrawingScale is a ratio pair     Sheet_ScaleHalvesAndDoubles...,                      PASS
                                                                        Export_OneKnownLengthMeasuresItsScale... (new)
014  geometry on demand, loud failure  sheetScene() builds per call     DrawingRegeneration_AFailedRegenerationCannot...     PASS
015  drawing at layer 4                CheckLayering.cmake table        architecture.layering -- FAILS THE BUILD             PASS
016  the scene is the export boundary  all 3 writers: 0 CAD headers     Export_EveryWriterRefusesAnInvalidScene...,          PASS
                                                                        Export_TheThreeFormatsPutTheSameGeometry...
017  sheets/views/annotations are      their IDs widen to ObjectId      *File_UsesTheExistingObjectEnvelopeAndNoNewKey       PASS
     document objects, not sub-objects
018  projection is sheet intent        convention stored on the sheet   first/third angle tests                              PASS
019  HLR is exact, not polygonal       HLRBRep_Algo, NOT PolyAlgo       hidden-line tests across drawing + geometry          PASS
020  a datum reference is a letter     Annotations.cpp datum path       datum letter / undefined datum tests                 PASS
021  an assembly view is ONE HLR       ViewSubject::Assembly, one pass  ViewAssembly / AssemblyDrawing tests                 PASS
022  a BOM is derived, has no identity billOfMaterials() recomputes     BomTable_* and Balloon_* tests                       PASS
023  a handler resolves references     drawing::registerHandlers        DrawingRegeneration_* tests                          PASS
024  a chamfer selection has identity  ChamferEdgeId + IdAllocator      Reference_AChamferFaceKeepsItsMeaning...             PASS
```

**ADR-019 checked by algorithm, not by name.** `OcctHiddenLine.cpp` includes
`HLRBRep_Algo.hxx` and constructs `HLRBRep_Algo`. OCCT's polygonal alternative
is `HLRBRep_PolyAlgo`, which appears nowhere. The ADR's claim is the choice of
algorithm, and that is what was verified.

**ADR-016 checked by absence, which is the only way.** `grep -c` for
`bettercad/core/document`, `bettercad/features`, `bettercad/assembly`, `BRep`
and `TopoDS` over `SvgWriter.cpp`, `DxfWriter.cpp` and `PdfWriter.cpp` returns
**0, 0, 0**. Their complete include lists are `FileIo.hpp`, `SceneNumbers.hpp`,
`TextEncoding.hpp`, `core/Units.hpp`, `io/DrawingExport.hpp`, `<format>`,
`<string>`, `<vector>`. A writer cannot recompute engineering semantics it
cannot see.

## FINAL INVARIANT AUDIT

Each invariant is listed with the test that asserts it. "Covered by the suite"
is not evidence; a named test that ran in the final binaries is.

```text
invariant                                        asserting test
3D model authoritative, drawing derived          View_TheFileHoldsIntentAndNoProjectedGeometry
drawing intent is canonical                      Persistence_SavingAfterAModelEditLeavesNoStaleDrawingInTheFile
references use stable semantic identity          Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel
topology index is never persistent identity      Reference_NoChamferReferenceIsStoredAsAPositionInTheFile
missing geometry becomes unresolved              Reference_AChamferFaceWhoseEdgeIsDeletedBecomesUnresolved
no silent rebinding                              Reference_AChamferFaceDoesNotRebindToAnIdenticalReplacementEdge
assembly views consume solved state              ViewAssembly / AssemblyDrawing tests
suppressed components are absent                 Balloon_OnASuppressedOccurrenceIsUnresolvedAndNeverRebinds
BOM quantities derive from active occurrences    DrawingReference_BoltedStack... quantities == {1, 4, 2}
dimensions are unit-safe                         Dimension_TheDrawingScaleNeverChangesTheMeasuredValue
regeneration cannot publish stale geometry       DrawingRegeneration_AFailedRegenerationCannotBeDrawnWithTheOldGeometry
save/load preserves intent, not derived geometry Persist_* round trips
export consumes DrawingScene only                ADR-016 above
```

## PROHIBITED-PATTERN AUDIT

```text
faceIndex 0   edgeIndex 0   shapeIndex 0   topologyIndex 0
bestMatch 0   firstMatch 0  reinterpret_cast 0
unordered_map 0   unordered_set 0
```

Zero across `src/` and `include/`. Zero unordered containers is the strongest
single determinism signal available: BOM numbering and scene ordering cannot
depend on hash iteration order because there is no hash container to depend on.

**AND THE LIMIT OF THAT SEARCH, WHICH RUN 0 DISCOVERED.** Every term returned 0
on a tree that *had* a positional drawing reference, because the field was
called `edge`. A name search is not an identity audit. The structural check is
`Reference_NoChamferReferenceIsStoredAsAPositionInTheFile`, which reorders a
chamfer's selections, re-saves, and requires the stored reference to be
byte-identical while the array order changed — something no word list can fake.

The only positional integer left in a persisted reference is
`FaceCopy::instance`, and it was examined rather than waved through: a pattern
instance ordinal follows from the pattern's own count and spacing, not from a
user-orderable list, so no edit reorders instances while leaving the solid
identical — and `LinearPatternFeature` guarantees that suppressing an instance
never renumbers another.

## HARNESS SELF-TEST

`qualification/verify-harness.cmd`, run against this milestone's own copy of
`qualify.cmd` before the final run: pointed at a preset that does not exist, the
harness fails real stages and **exits 3**. The harness once ended `exit /b 0`
whatever happened, so a failed stage reached nobody; this is the regression for
that, and it also proves a repeat filter matching nothing is reported as a
failure rather than silently skipping the determinism gate.

That mattered here. The final run's release determinism stage DID fail, and the
harness surfaced it as a non-zero exit rather than burying it in a log.

## FROZEN TREE AND SOURCE FINGERPRINT

```text
qualification candidate   0992aec + the two qualification-gap tests
apps                      d8b08545dbc80be58b4827977dcceaadeb60c82d
include                   0dec3a71f8a7334f8c03241d97fcbf27bf636bf4
src                       a552f8b5364c413e8cfd9f417e4a1dd8f760ac99
tests                     c22084fd2d0220a44d606d7661c9a01263513fb2
examples                  2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake                     a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt            a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json         951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Recorded before the first build and again after the last test run: **identical**.
Recomputed after the controlled rerun and before the commit: **identical**. Only
`tests/` differs from commit `0992aec` (`454eb29c` → `c22084fd`), which is
exactly the two tests this milestone added and nothing else.

## THE EVIDENCE / TREE CIRCULARITY, RESOLVED EXPLICITLY

The fingerprint covers **eight paths**, and the harness computes them from a
scratch index so untracked new files count:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`docs/` is deliberately outside it, and that is checkable rather than asserted:
the only occurrence of `docs/` anywhere in the build system is a **comment** in
`tests/CMakeLists.txt`. No target consumes it, no test reads it, nothing is
generated from it. Writing this document therefore cannot change what was
qualified.

So the qualified fingerprint and the committed fingerprint are both taken over
those eight paths, and the closeout changes only `docs/` and `TODO.md` — neither
of which is in them. **Tree A is tree B.** The one thing that would break this
is a closeout that touched a fingerprinted path; if that ever happens, the
qualification is void and re-run, which is exactly what happened twice in this
milestone when tests were added.

## CLEAN QUALIFICATION — THE FINAL RUN

```text
stage                        exit   wall clock
debug configure                 0   08:56:19 -> 08:56:34
debug clean (attempt 1)         0   08:56:36
debug build                     0   08:56:36 -> 09:11:01
debug no-op rebuild             0   09:11:02      compiled 0, linked 0
debug ctest                     0   09:11:02 -> 09:14:31   2258/2258
release configure               0   09:14:31 -> 09:14:37
release clean (attempt 1)       0   09:14:38
release build                   0   09:14:38 -> 09:34:08
release no-op rebuild           0   09:34:10      compiled 0, linked 0
release ctest                   0   09:34:10 -> 09:37:23   2258/2258
debug-shared configure          0   09:37:23 -> 09:37:29
debug-shared clean (attempt 1)  0   09:37:30
debug-shared build              0   09:37:30 -> 09:51:37
debug-shared no-op rebuild      0   09:51:38      compiled 0, linked 0
debug-shared ctest              0   09:51:38 -> 09:55:27   2258/2258
repeat release (5x)             8   09:55:27 -> 10:10:53   *** FAILED, see below
repeat debug (5x)               0   10:10:53 -> 10:27:39   11290 = 2258 x 5
```

```text
Debug          2258 / 2258     0 warnings
Release        2258 / 2258     0 warnings
Debug-shared   2258 / 2258     0 warnings
```

Every clean was first-attempt. All three test totals come from that preset's own
log written by THIS run — a point worth making, because the harness overwrites
each log in place, so mid-run the release and debug-shared logs still held the
previous run's contents and reading them then would have cited a tree that no
longer exists.

## FRESH-BINARY PROOF

Each build was followed immediately by a second build of the same preset. The
only edge that ran in any of them was `Checking git revision`, which is dirty by
design and produces no recompile and no relink:

```text
debug          [1/8]  Checking git revision
release        [1/8]  Checking git revision
debug-shared   [1/14] Checking git revision
```

So the binaries CTest ran are the binaries the build produced.

**AND THE TWO NEW TESTS RAN IN ALL THREE PRESETS, not just Debug.** Source
existing is not evidence; CTest discovering it, running it and passing it is:

```text
                                                   debug  release  debug-shared
Export_OneKnownLengthMeasuresItsScale...           Passed  Passed   Passed
Export_AnUnwritableDestinationFailsAndLeaves...    Passed  Passed   Passed
```

In Debug they are `#750` and `#744` of 2258, and the log shows `Start`, then the
numbered result line, for each.

`ctest -N` lists **2258** tests, 2 more than the 2256 of the first (invalidated)
run, and the 2 are exactly those names.

## DETERMINISM

```text
repeat debug (in the qualification)   11290 "Passed" lines = 2258 x 5   PASS
repeat release (in the qualification) FAILED -- the recorded environment fault
repeat release (controlled rerun)     11290 "Passed" lines = 2258 x 5   PASS
                                      0 occurrences of ***Failed, Not Run,
                                      ***Exception or Permission denied
```

Counted from the logs, not read off CTest's summary line, which reports tests
rather than runs.

**The filter is `[A-Za-z]`, which selects all 2258 tests.** Every earlier P14
milestone repeated a drawing-shaped subset; this gate repeats the whole suite,
five times, in two presets. That is deliberate: P14's claim is that a drawing
stays attached to its model through regeneration, save, load, undo,
configuration switching and export, and that reaches essentially the whole
system.

### THE ONE STAGE THAT FAILED, AND WHY IT IS NOT A DEFECT

Reported before the conclusion, because a failed stage is a failed gate until it
is understood.

```text
release repeat, cli.refmod.build:
    repeat 1  Passed 1.69 s
    repeat 2  Passed 1.57 s
    repeat 3  Passed 1.49 s
    repeat 4  ***Failed 0.45 s
      DrawnAngleBracket: DXF export failed: cannot replace
      '.../build/release/tests/cli-output/refmod\drawing_angle_bracket.dxf':
      Permission denied
    -> 14 dependents ***Not Run: the failed test is the refmod_cli
       FIXTURES_SETUP, so CTest correctly refused to run what depends on it
```

A Windows sharing violation on an **atomic replace** of a file inside the
OneDrive-synchronised build tree — a file the same test had already written
successfully three times in the same run. `TODO.md` records this exact signature
for four earlier milestones:

```text
P14-DIM-001     debug repeat
P14-ANNO-001    release repeat
P14-BOM-001     debug repeat -- "passed three times in one run before failing
                on the fourth repeat", which is this shape precisely
P14-REFMOD-001  BOTH repeat presets in one run
```

and records that one controlled rerun passed every time. **This is the fifth
milestone.**

Nothing was relaxed in response. `rerun-release-repeat.cmd` re-runs the WHOLE
release determinism stage with the SAME `[A-Za-z]` filter — all 2258 tests, five
times over — and exits non-zero if it fails again. Not a retry of the failing
test, not a narrowed filter, not an exclusion. The debug stage passed and was
deliberately NOT re-run: re-running a passing stage would be choosing which
result to keep.

The rerun passed, and the test that failed is in its log five times:

```text
Test #2182: cli.refmod.build  ->  Passed 2.14, 2.07, 2.26, 2.24, 2.46 sec
75 = 15 x 5 cli.refmod.* results, so no dependent was skipped this time
```

**What this does and does not establish.** It establishes that the release
determinism gate passes on this tree. It does not establish that the gate passed
on the first attempt, and this document does not claim it did. The source tree
did not change between the failure and the rerun — the fingerprint above is
identical across both — so the difference is the filesystem, which is the
diagnosis.

## CROSS-MILESTONE VALIDATION

Every gate below was exercised by the full suite in all three presets and five
times over in two. Each is listed with the test that owns it, because "the suite
covers it" is not evidence.

```text
PRODUCTION REFERENCE SUITE    8 models, RM-DWG-01..08, and the coverage matrix
                              is an ASSERTION:
                              DrawingReference_TheSuiteCoversEveryQualified-
                              DrawingCapability fails naming any qualified
                              capability with no reference-model owner
DIMENSIONS / ANNOTATIONS      linear, horizontal, vertical, aligned, angular,
                              radius, diameter, ordinate (signed), notes,
                              leaders, centrelines, centre marks, hole callouts,
                              datums, surface finish, feature-control frames.
                              Scale independence:
                              Dimension_TheDrawingScaleNeverChangesTheMeasured-
                              Value, and paper-sized symbols at every scale
                              (Annotation_*IsTheSameSizeOnPaperAtEveryScale)
STABLE REFERENCES             Reference_ADimensionNeverMovesToAnIdenticalFace-
                              OfAnotherFeature, _ACalloutNeverMovesToAnIdentical-
                              Hole, _ABalloonRecoversTheSameOccurrenceAndNever-
                              TheOther, _RecoveryRestoresTheSameTargetAnd-
                              RebindingIsRefused. Every no-rebind fixture is
                              built so a rebinding implementation would SUCCEED
CHAMFER REFERENCE CLOSURE     see below
REGENERATION                  DrawingRegeneration_AFailedRegenerationCannotBe-
                              DrawnWithTheOldGeometry;
                              DrawingReference_ClampSetDrawingIsNotCurrentWhen-
                              ItsAssemblyBreaks
PERSISTENCE                   create -> save -> destroy -> load -> regenerate
                              -> compare, across all eight reference models;
                              Persistence_SavingAfterAModelEditLeavesNoStale-
                              DrawingInTheFile; malformed-file cases per object
                              kind; version 1 AND 2 both load
UNDO / REDO                   Command_AHundredMoveUndoRedoCyclesLandOnExactly-
                              TheSameNumbers; _ADivergentEditClearsTheRedoStack;
                              _AFailedCommandLeavesTheRedoBranchIntact;
                              _ARedoneBalloonShowsTheCurrentItemNumberAndNever-
                              AStoredOne
BOM / BALLOONS                quantities == {1, 4, 2} over 7 occurrences of 3
                              parts; two balloons on two instances of ONE bolt
                              share an item number and keep different
                              occurrence references
CLI                           15 cli.refmod.* process tests on the real
                              executables; CLI/core equivalence to identical
                              canonical JSON, identical IDs and byte-identical
                              SVG, DXF and PDF; cli.drawing.batch.fails and
                              .wrote-nothing prove an inner failure reaches the
                              process exit status and writes nothing
PDF / SVG / DXF               read back with parsers sharing no code with the
                              writers; entity count == scene item count in DXF
                              and SVG for all eight models, so a silently
                              dropped primitive fails
```

## CHAMFER REFERENCE CLOSURE

The gate that blocked run 0, verified here on the final tree rather than taken
from `P14-STREF-001`'s word:

```text
reorder the selections            reference follows its own
insert another selection first    unchanged
delete an unrelated selection     unchanged
delete the referenced selection   Unresolved
add back an IDENTICAL curve       stays Unresolved
restore the same selection        recovers  (target recovery)
undo / redo the edit              recovers / goes again
save -> load                      same selection
reorder -> save -> load           same selection
resolve repeatedly, and after     one answer, exactly
regeneration
no positional form in the file    reorder, re-save, stored reference
                                  byte-identical while the array order changed
```

All eleven have named tests, all ran in the final binaries in all three presets.
`FaceSelector::edge` is a `ChamferEdgeId`, not a `std::uint32_t`: the type
changed, so the compiler enumerated every consumer rather than letting anything
keep treating it as a position.

## PHYSICAL SCALE READ-BACK

The gate run 0 could not close, because no test measured it. One 100 mm model
edge, three views at three scales on one A1 sheet, the paper length taken
through the production projection and then read back out of each file.

```text
           expected   projection              DXF      SVG      PDF
1:1        100 mm     100.00000000000001      100.0    100.0     99.99997638888885
1:2         50 mm      50.00000000000006       50.0     50.0     50.00000583333332
2:1        200 mm     200.0                   200.0    200.0    200.00002333333327
max error             5.7e-14 mm              exact    exact     2.36e-05 mm
```

DXF and SVG are exact at their writers' four-decimal resolution. **The PDF
deviation is explained, not tolerated:** a PDF page is in points, 72 to the
inch, so a millimetre is 2.8346 points; the writer prints four decimals, so one
printed step is 1e-4 pt / 2.8346 pt/mm = **3.5e-05 mm**. The largest observed
error, 2.36e-05 mm, is inside a single printed step. The tolerance is 1e-3 mm
and was chosen for that reason, not to make the test pass.

**The test proved its own sensitivity while it was being written.** Its first
band filter matched a run by its midpoint and picked up the **841 mm A1 sheet
border** instead of the 50 mm drawing. Requiring both endpoints inside the
view's neighbourhood fixed it — and a test that catches the page frame will
catch a wrong scale.

## UNWRITABLE EXPORT CONTROL

The other gate run 0 could not close. The destination's parent directory does
not exist, so writing cannot succeed for a reason nothing in the document can
predict.

```text
              failed   code      names the path   file left   temp left
exportSvg     yes      IoError   yes              no          no
exportDxf     yes      IoError   yes              no          no
exportPdf     yes      IoError   yes              no          no
```

`IoError`, not `InvalidArgument` or a parse error: the scene was valid and the
destination was not. Nothing was left behind — neither the target file nor the
temporary the writers write through, and the directory was not created.

**And the same scene then exported successfully to a real directory in all three
formats, each file non-empty.** That is what makes the three refusals evidence
about the destination rather than about the drawing.

## WARNINGS

```text
build-debug 0    build-release 0    build-debug-shared 0
rebuild-debug 0  rebuild-release 0  rebuild-debug-shared 0
```

`grep -ci warning` over all six logs of the final run. Not inferred from an
earlier run. Under the 22 warning flags `cmake/BetterCADCompilerOptions.cmake`
sets, `-Werror` among them.

```text
unexpected warnings: 0
```

## FINAL ADVERSARIAL REVIEW

P14 attacked as one system across its boundaries — stable reference →
regeneration → scene → persistence → commands → CLI → export — rather than
milestone by milestone. Each question is answered by a test that ran in the
final binaries.

```text
 1  positional persistent reference?   NO. Reference_NoPersistedReference-
                                       CarriesAnIndexIntoTheKernel and
                                       _NoChamferReferenceIsStoredAsAPosition-
                                       InTheFile (structural: reorder, re-save,
                                       byte-identical)
 2  chamfer survives reorder/insert/    YES x3, named tests above
    unrelated delete?
 3  deleted target -> unresolved?       YES
 4  identical replacement adopted?      NO
 5  undo restores a retired identity?   YES -- and the first rule I wrote
                                        forbade it and broke undo; the suite
                                        caught that, not review
 6  save/load preserves identity?       YES, including after a reorder
 7  configuration switch: Resolved ->   YES. DrawingReference_GuardedFrame-
    Unresolved -> same Resolved?        BalloonGoesUnresolvedAndComesBack
 8  stale geometry marked current?      NO
 9  undo/redo exact, IDs included?      YES, 100 cycles land on the same numbers
10  BOM groups definitions, keeps       YES. {1,4,2}; two balloons on two
    occurrence identity?                instances keep distinct references
11  balloons resolve by occurrence?     YES, never a stored number
12  CLI failure reaches process exit?   YES, and it writes nothing
13  writers see only DrawingScene?      YES. 0 CAD headers in all three
14  all three formats keep scale?       YES, measured above
15  unwritable export corrupts?         NO, nothing left behind
16  stale binary or zero-match filter   NO. No-op rebuild proves fresh
    make this green?                    binaries; verify-harness proves a
                                        failed stage exits 3 and a filter
                                        matching nothing is a failure
```

```text
findings:                0 new
new production defects:  0
remaining blockers:      0
```

Two defects WERE found during this phase's work, both in `P14-STREF-001`'s
chamfer fix and both by the test suite rather than by reading the diff: a rule
that made undo impossible, and an implicit conversion that silently retired a
selection's identity in the one workflow meant to repair a broken reference.
Both were fixed before that milestone closed and are recorded there. Neither is
outstanding.

## KNOWN LIMITATIONS

These are carried, and P14 is qualified WITH them rather than despite them.

**A hole's POSITION cannot be dimensioned.** A cylindrical face may be the
target of a radius or a diameter and of nothing else (`P14-DIM-001`), and a
bore's axis has no semantic name (ADR-012). This is the commonest dimension on a
machining drawing and it has no spelling in this build. `RM-DWG-04` dimensions
between the plate's datum edges instead.

**GD&T symbols reach SVG only.** No single-byte encoding and none of PDF's
fourteen standard fonts has a glyph for position, cylindricity, straightness,
flatness or runout, so they are written as `?` in PDF and DXF — visibly missing,
which is better than an exporter inventing GD&T semantics. Closing it means
embedding a font.

**Cross-preset byte identity of exported files is not pinned by an assertion.**
Each preset writes the same bytes as the core API within its own run, and the
writers are deterministic by construction, but no test compares a
Release-written file with a Debug-written one.

**The build-location decision is open, and BLOCKED BY A DEFECT.** Building
outside the source tree fails the GUI target in all three presets, because
`windeployqt` resolves the Qt runtime relative to the executable it is deploying
— `<exe>/../../<toolchain key>/bin` — which names the real Qt only when the
build sits inside the source tree. Qt's bin on PATH, running `windeployqt` from
Qt's bin, and pre-placing `Qt6Core.dll` beside the executable were each tried
and none changed it. **P14 is therefore qualified on a machine configuration
known to inject a spurious failure roughly once per milestone** — it did so
here, on this gate. That is a property of the environment, not of the product,
and the fix is its own piece of work.

## FINAL TREE EQUALITY

```text
FINAL_TREE_EQUALITY
```

## RESULT

```text
TASK:            P14-QUAL-001 — Full P14 Qualification
RESULT:          PASS
SCOPE:           the final run only. Runs 1 and 2 were green about trees that
                 are not the committed tree and are not cited.
TESTS:           2258/2258 in Debug, Release and Debug-shared, each from clean,
                 0 warnings in all six build and rebuild logs, fresh binaries
                 in all three. 11290 = 2258 x 5 in both determinism presets --
                 the release stage on a controlled rerun after the recorded
                 OneDrive replace fault, the debug stage first time.
VALIDATION:      15 of 15 ADRs PASS with production AND test evidence, none
                 NOT REACHED. All 17 predecessor milestones complete. Every
                 invariant and every adversarial question answered by a named
                 test that ran in the final binaries. The two gates run 0 could
                 not close -- physical scale and unwritable export -- now have
                 tests, and the scale gate's nine read-back values are recorded
                 with their errors.
ADVERSARIAL:     0 new findings, 0 new production defects, 0 remaining blockers.
NOT CLEAN FIRST
TIME:            the release determinism stage failed once, on the recorded
                 environment fault, and passed on one controlled rerun of the
                 same stage with the same filter. This document says so rather
                 than presenting the gate as clean on the first attempt.
EVIDENCE:        this document and qualification/
TODO:            P14-QUAL-001 -> [x]; P14 -> QUALIFIED.
                 The build-location decision stays OPEN with a named blocker.
```

**P14 — Technical Drawings is QUALIFIED.**

## REVISION

```text
2026-09-25  Run 0: audit at 56f5a34. RESULT BLOCKED at the precondition on
            P14-STREF-001's open "no silent rebinding" gate. No build.
2026-09-26  P14-STREF-001 closed at 0992aec (ADR-024), which unblocked this.
2026-09-26  Runs 1 and 2 green and INVALIDATED: each of the two missing
            qualification gates needed a test, and adding one changed tests/.
2026-09-26  Run 3, the final run, on tests c22084fd. RESULT PASS, with the
            release determinism stage passing on one controlled rerun.
```


---

# APPENDIX — RUN 0: THE BLOCKED AUDIT, 2026-09-25

Kept verbatim. It is the record that found the blocker, and it is the reason
`P14-STREF-001` was closed before this milestone could proceed. Its RESULT of
BLOCKED was correct when written; nothing in it has been edited to agree with
the outcome above.

## Run 0, as written on 2026-09-25

```text
STATUS:    BLOCKED — a required predecessor is incomplete   [SUPERSEDED]
MILESTONE: P14-QUAL-001
DATE:      2026-09-25
BASELINE:  56f5a34 (P14-REFMOD-001)
```

> This status was correct on 2026-09-25 and is SUPERSEDED by the final run at
> the top of this document. The blocker it names — `P14-STREF-001`'s open "no
> silent rebinding" gate — was closed by ADR-024 on 2026-09-26.

## RESULT

```text
TASK:      P14-QUAL-001 — Full P14 Qualification
RESULT:    BLOCKED
BLOCKER:   P14-STREF-001 is not complete. Its gate "no silent rebinding" is
           NOT MET, in TODO.md and in its own evidence, and the defect is
           present in this tree.
STOPPED:   at the precondition, before freezing the tree and before any
           qualification build. Nothing was built, no preset was run, and no
           gate was marked.
EVIDENCE:  this document. It records the audits that WERE run -- they are what
           established the blocker -- and states plainly which gates were not
           reached.
TODO:      P14-QUAL-001 stays [ ], with the blocker recorded against it.
           No checkbox was ticked anywhere.
```

**P14 WAS NOT QUALIFIED AT THE TIME OF RUN 0.** It is now — see the final run
at the top of this document.

## WHY THIS STOPPED AT THE PRECONDITION

The precondition for this milestone is that every preceding P14 milestone
required by `TODO.md` is complete, and that an incomplete predecessor means
BLOCKED. One is incomplete. Beyond that, the final gate for this milestone
requires `no silent rebinding` — the exact gate that is open — so no amount of
building could turn this tree into a PASS. Running three clean qualifications
and ten repeat suites first would have cost hours and produced a document whose
RESULT still had to read BLOCKED.

The audits below were still worth running, and were run before stopping,
because the question that matters to the scope decision is not "is there a
blocker" but "is that the ONLY blocker". It is, plus one documentation defect
that this audit fixed.

## BASELINE

```text
branch        main
HEAD          56f5a34a1eb8527f92e981aeadbf6ff3ff70639e
origin/main   56f5a34a1eb8527f92e981aeadbf6ff3ff70639e   (identical)
HEAD^{tree}   9e1eccb72b7da1408b969d8f738b254a9ba1b139
working tree  clean -- git status --porcelain empty
              git diff --check reports no whitespace errors

cmake         4.4.2
ninja         1.13.2
g++           16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, r4)
target        x86_64-w64-mingw32
ctest         4.4.2
git           2.55.0.windows.5
generator     Ninja
binaryDir     build/<presetName> under the source directory
platform      Windows-11-10.0.26200-SP0
cpu           AMD64 Family 25 Model 80 (AuthenticAMD)
```

The tree was clean at the start and no production or test file was touched by
this milestone, so there is nothing to classify.

## TODO AUDIT

Read from the current `TODO.md`, counting checkboxes per milestone section
rather than trusting a previous report.

```text
Milestone          header   [x]  [ ]   state
P14-ARCH-001       DONE      11    0   complete
P14-SHEET-001      DONE      16    0   complete
P14-VIEW-001       DONE      14    0   complete
P14-VIEW-002       open      12    0   complete
P14-HLR-001        open      12    0   complete
P14-DIM-001        open      15    0   complete
P14-ANNO-001       open      13    0   complete
P14-TOL-001        open      12    0   complete
P14-ASM-001        open      11    0   complete
P14-BOM-001        open      14    0   complete
P14-STREF-001      open      13    1   *** ONE OPEN ITEM ***
P14-REGEN-001      open      13    0   complete
P14-CMD-001        open      14    0   complete
P14-PERSIST-001    open      15    0   complete
P14-CLI-001        open      15    0   complete
P14-EXPORT-001     open      15    0   complete
P14-REFMOD-001     DONE      17    0   complete
P14-QUAL-001       open       0   22   this milestone
```

Sixteen of seventeen predecessors are complete. The `header` column records
that most completed sections are not titled `DONE —`; that is a cosmetic
inconsistency in `TODO.md`, not a gate, and it was left alone.

**The one open item, quoted from `TODO.md`:**

```text
* [ ] Prevent silent rebinding — NOT MET. A chamfer face is named
      {role = Chamfer, edge = N} where N is the POSITION of an edge
      reference in ChamferDefinition::edges. Reordering that list leaves
      the reference resolving — to different material. Measured in
      Reference_AChamferFaceIsNamedByItsPositionInTheChamfersEdgeList;
      every other reference path holds, including against identical
      survivors deliberately left in place
```

`TODO.md` also records why it was not closed in that milestone: closing it
changes `ChamferDefinition` and its file format, and re-qualifies the committed
reference models that contain chamfers — artifacts three phases are qualified
against. That is a scope decision, and `TODO.md` says so.

## MILESTONE EVIDENCE AUDIT

Every `docs/verification/P14-*/` directory exists and carries a `README.md`.
Each was checked for unfilled placeholders and for agreement between its header
status and its final RESULT.

```text
Milestone          size   placeholders    header               final
P14-ARCH-001        40K   0 (3 prose)     PASS                 PASS
P14-SHEET-001      2.4M   0               PASS                 PASS
P14-VIEW-001       2.8M   0               PASS                 PASS
P14-VIEW-002       3.0M   0               PASS                 PASS
P14-HLR-001        3.2M   0               PASS                 PASS
P14-DIM-001        9.6M   0               PASS, one rerun      PASS
P14-ANNO-001       7.7M   0 (1 prose)     PASS, 2nd qual       PASS
P14-TOL-001        5.8M   0               PASS                 PASS
P14-ASM-001        5.9M   0               PASS                 PASS
P14-BOM-001         11M   0               PASS, rerun gate     PASS
P14-STREF-001      6.0M   0               BLOCKED, one gate    BLOCKED
P14-REGEN-001      6.1M   0               PASS                 PASS
P14-CMD-001        6.2M   0               PASS                 PASS
P14-PERSIST-001    6.2M   0               PASS                 PASS
P14-CLI-001        6.3M   0               PASS                 PASS
P14-EXPORT-001     6.4M   0               PASS                 PASS
P14-REFMOD-001      12M   0               *** PENDING ***      PASS
```

The four "placeholder" hits are the word used in prose — ARCH-001 quoting the
rule against "writing a stub, a placeholder type" to start a milestone, and
ANNO-001 stating that its own evidence carries no placeholders. No evidence
directory has an unfilled template.

`P14-STREF-001`'s evidence agrees with `TODO.md`: its header reads
`STATUS: BLOCKED on one gate -- no silent rebinding` and its RESULT reads
`BLOCKED`. The record is honest; nothing was papered over.

### DEFECT FOUND AND FIXED: a stale status header

`P14-REFMOD-001`'s evidence header still read `STATUS: PENDING QUALIFICATION`
while the same document's RESULT section, added at the end of that milestone,
reads `PASS` with three presets at 2245/2245 behind it. The header was written
before the qualification ran and was not updated when the result was filled in.

A document that states two different statuses about itself is a failed evidence
gate, so this audit corrected the header to match the document's own RESULT and
the qualification logs beside it. This is not evidence being rewritten to look
cleaner: the milestone did pass, the logs are in the same directory, and what
was removed was a false statement.

Nothing else in that document changed, and no other milestone's evidence was
edited.

## ADR AUDIT

The repository carries fourteen P14 ADRs, `ADR-010` to `ADR-023` — more than
the brief listed, so repository truth was used.

Two were audited mechanically here, because they are the two a regression would
hide in. The remaining twelve were **not** re-audited against a fresh build,
because the milestone stopped before the qualification builds; they are
recorded as NOT REACHED rather than PASS.

```text
ADR      subject                                  this audit
ADR-015  drawing module at layer 4                PASS, enforced by the build
ADR-016  the scene is the export boundary         PASS, verified here
ADR-012  references name semantic geometry only   VIOLATION, see below
ADR-010, 011, 013, 014, 017, 018, 019,
ADR-020, 021, 022, 023                            NOT REACHED
```

**ADR-016 — PASS.** The three writers' complete include lists are
`FileIo.hpp`, `SceneNumbers.hpp`, `TextEncoding.hpp`, `core/Units.hpp`,
`io/DrawingExport.hpp`, `<format>`, `<string>` and `<vector>`. No writer
includes `Document`, a BRep header, the assembly solver, the dimension
calculator or the BOM calculator. Every apparent `Document` mention is a
function name — `svgDocument`, `dxfDocument`, `pdfDocument` — plus one comment
in `PdfWriter.cpp` recording that there is deliberately no PDF document ID, for
determinism.

**ADR-015 — PASS, and not on my word.** `tests/architecture/CheckLayering.cmake`
holds the layer table and fails the build, so this ADR cannot regress silently.

**ADR-012 — VIOLATION, for one face role.** See the invariant audit.

## FINAL INVARIANT AUDIT

Of the fifteen invariants required at the final gate, two are affected by the
open gate. The rest were not re-proven against a fresh build and are recorded
as NOT REACHED rather than claimed.

```text
invariant                                    state
model references use stable semantic         VIOLATION for FaceRole::Chamfer
identity                                     only. Every other role is named
                                              by the sketch entity that
                                              generates the face
no silent rebinding                          VIOLATION, measured
topology index is never persistent identity  HOLDS for the kernel. A
                                              positional index into stored
                                              INTENT is persisted -- below
the other twelve                             NOT REACHED
```

### The violation, in the code as it stands at 56f5a34

```text
include/bettercad/core/document/References.hpp:97
    std::optional<std::uint32_t> edge{};

include/bettercad/core/document/References.hpp:104-106
    "a chamfer face names an edge reference from 1"
```

`edge` is a **position** in `ChamferDefinition::edges`, and that vector is
ordinary stored intent a user may reorder. So a drawing reference to a chamfer
face is one stable link — an `ObjectId` — followed by one positional link.
`Reference_AChamferFaceIsNamedByItsPositionInTheChamfersEdgeList` reorders the
list, leaves the solid identical and the reference untouched, and measures the
reference resolving to the face 60 mm away. The test is written to fail if the
gap is ever closed, so the evidence cannot go stale.

Shortening the list is safe: the last position stops matching and the reference
becomes `Unresolved`, which is correct. Only reordering is dangerous.

### Two things this audit establishes that were not previously written down

**The prohibited-name search cannot find this defect.** Searching `src/` and
`include/` for `faceIndex`, `edgeIndex`, `shapeIndex`, `topologyIndex`,
`bestMatch`, `firstMatch` and `reinterpret_cast` returns **0** for every term.
The field is called `edge`, not `edgeIndex`, so a name-based audit returns a
clean sheet over a tree that has the defect. A future audit must not read those
zeros as an absence of positional identity.

**The schema-audit test cannot find it either.**
`Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel` saves a document
carrying every drawing reference class and asserts the text contains no
`face_index`, `faceIndex`, `edge_index`, `edgeIndex`, `shape_index`,
`shapeIndex`, `topology`, `ordinal`, `pointer`, `address`, `handle` or
`traversal`. Two reasons it passes anyway: the serialized key is `"edge": n`
(`src/io/json/DatumJson.cpp:220`), which is not in that list, and the fixture
builds no chamfer, so no chamfer reference is in the file being searched. The
test's name is precise — an index **into the kernel** — and is true as written.
It simply does not cover this.

Closing the gap should extend that test's fixture and its forbidden list, or the
same blind spot will survive the fix.

### What is NOT a violation, checked rather than assumed

`FaceCopy::instance` (`References.hpp:76`) is also a positional integer — a
pattern instance ordinal. It was examined and is materially different. The
ordinal is determined by the pattern's own `PatternDirection` count and
spacing, not by a user-orderable list, so no operation reorders instances while
leaving the solid identical; and `LinearPatternFeature.hpp:93` guarantees that
"suppressing an instance never renumbers another: index 3 is index 3 whether 2
is suppressed or not". A reference to instance 3 cannot be moved by an edit that
preserves the geometry.

## PROHIBITED-PATTERN AUDIT

```text
term                src/ + include/ hits    verdict
faceIndex                              0
edgeIndex                              0
shapeIndex                             0
topologyIndex                          0
bestMatch                              0
firstMatch                             0
reinterpret_cast                       0
unordered_map                          0    no unordered container anywhere
unordered_set                          0    in the library or its headers
nearest                               20    all canonicalisation or geometry
closest                                2    all canonicalisation or geometry
```

Every `nearest` and `closest` hit was read. None is proximity-based reference
resolution or nondeterministic selection. They are: the canonical representative
point of a line or plane — "the point of the line nearest the origin", which is
how a signature is made deterministic and is the opposite of a proximity match;
plane-frame axis choice from a normal; the definition of occlusion in
hidden-line removal, "the material nearest the viewer"; and two diagnostic
strings about edges whose nearest ends do not meet.

**Zero unordered containers in `src/` and `include/`** is the strongest single
determinism signal in this audit: BOM numbering and scene ordering cannot depend
on hash iteration order, because there is no hash container to depend on.

The audit's own limitation is recorded above: a name-based search returns 0 on a
tree that has a positional identity defect.

## GATES NOT REACHED

Recorded explicitly, because an audit that stops must say where it stopped.
None of the following was run, and none may be inferred from any earlier
milestone's PASS:

```text
Freeze final P14 tree                     NOT REACHED
Source fingerprint                        NOT REACHED
Qualification harness self-test           NOT REACHED
Clean Debug qualification                 NOT REACHED
Clean Release qualification               NOT REACHED
Clean Debug-shared qualification          NOT REACHED
Fresh-binary proof                        NOT REACHED
Repeated determinism qualification        NOT REACHED
Production reference suite                NOT REACHED
Dimensions / annotations                  NOT REACHED
Stable references                         BLOCKED -- the gate that blocks
Regeneration                              NOT REACHED
Persistence                               NOT REACHED
Undo / redo                               NOT REACHED
BOM / balloons                            NOT REACHED
CLI                                       NOT REACHED
PDF / SVG / DXF                           NOT REACHED
Independent scale validation              NOT REACHED
Failure controls                          NOT REACHED
Final adversarial review                  NOT REACHED
Unexpected warnings = 0                   NOT REACHED
Qualified tree == committed tree          NOT REACHED
Mark P14 qualified                        NOT DONE
```

## QUALIFICATION-RESULT MATRIX

```text
Gate                                Result
TODO completeness                   FAIL   -- P14-STREF-001 has one open item
ADR contracts                       FAIL   -- ADR-012 violated for chamfer faces
Debug                               NOT RUN
Release                             NOT RUN
Debug-shared                        NOT RUN
Determinism                         NOT RUN
Production references               NOT RUN
Dimensions / annotations            NOT RUN
Stable references                   FAIL   -- no silent rebinding, measured
Regeneration                        NOT RUN
Persistence                         NOT RUN
Undo / redo                         NOT RUN
BOM / balloons                      NOT RUN
CLI                                 NOT RUN
PDF                                 NOT RUN
SVG                                 NOT RUN
DXF                                 NOT RUN
Adversarial review                  NOT RUN
Unexpected warnings = 0             NOT RUN
Qualified tree == committed tree    NOT RUN
```

No gate is averaged. Two fail, one of them is the blocker, and the rest were not
run.

## HOW THE EVIDENCE CIRCULARITY IS RESOLVED

Recorded here because it is asked for explicitly and because it will apply when
this milestone is re-run.

The project's established fingerprint, implemented in every P14 milestone's
`qualification/qualify.cmd`, covers exactly eight paths:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

It is taken from a scratch index — `GIT_INDEX_FILE`, `git read-tree HEAD`,
`git add -A`, `git write-tree --prefix=` — so it covers untracked new files as
well as modified ones, and it is recorded before the first build and again after
the last test run.

`docs/` is deliberately outside it. `CLAUDE.md` states the policy directly:
documentation that cannot affect the executable or the tests may follow the
project's existing policy. Evidence is documentation; it is not compiled, it is
not a test input, and no build target depends on it. So writing evidence after a
qualification cannot change what was qualified, and the circularity does not
arise: the qualified fingerprint and the committed fingerprint are both taken
over the eight source paths, and evidence is added outside them.

`P14-REFMOD-001` is the worked example — its eight tree IDs are identical before
the first build, after the last test run, before the commit, and read back from
the commit object — and no source path lies under `docs/`.

## KNOWN LIMITATIONS OF THIS AUDIT

**Twelve of fourteen ADRs and twelve of fifteen invariants were not re-proven.**
They are marked NOT REACHED above and must not be read as PASS. An earlier
milestone passing them is not this milestone passing them, which is this
milestone's own rule.

**No build ran, so nothing here says anything about the current tree's
compilation, warnings, test count or determinism.** The most recent such
evidence is `P14-REFMOD-001`'s, at this same commit: 2245/2245 in Debug, Release
and Debug-shared with 0 warnings, and both determinism repeat stages passing on
a controlled rerun. That is a predecessor's evidence, not this milestone's.

**The OneDrive build-output decision is still open**, and it should be taken
before this milestone is re-run rather than after. It has failed a determinism
repeat stage in four milestones now, both presets in the most recent one. Its
`binaryDir` lives in `CMakePresets.json`, inside the eight paths the fingerprint
covers, so changing it after a qualification voids that qualification. Taking it
first costs one decision; taking it afterwards costs a second full
qualification.

## WHAT UNBLOCKS THIS MILESTONE

Two routes. Both are scope decisions, and neither is a coding agent's to make.

**Close the gap.** Give each chamfer edge reference an identity of its own, name
the face by that identity rather than by its position, migrate the file format,
extend `Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel`'s fixture and
forbidden list to cover a chamfer, and re-qualify the committed reference models
that contain chamfers. That last step touches artifacts P11, P12 and P13 are
qualified against, which is why `P14-STREF-001` did not do it. P14 would then be
qualifiable with the invariant genuinely met.

**Accept it, in writing, and re-scope.** Record the positional chamfer reference
as a documented P14 limitation, and change this milestone's final gate so it no
longer claims `no silent rebinding` without qualification. This is cheaper, and
it is honest only if the gate text changes with it — marking the gate met while
the defect stands is the false checkbox `CLAUDE.md` exists to prevent.

## REVISION

```text
2026-09-25  Audit run at 56f5a34. RESULT BLOCKED on P14-STREF-001.
            Fixed one evidence defect the audit found: P14-REFMOD-001's stale
            PENDING QUALIFICATION header.
```
