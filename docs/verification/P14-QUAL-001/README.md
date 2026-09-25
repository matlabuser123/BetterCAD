# P14-QUAL-001 — Full P14 Qualification

```text
STATUS:    BLOCKED — a required predecessor is incomplete
MILESTONE: P14-QUAL-001
DATE:      2026-09-25
BASELINE:  56f5a34 (P14-REFMOD-001)
```

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

**P14 IS NOT QUALIFIED.**

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
