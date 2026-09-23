# P14-BOM-001 — Bill of Materials and Balloons

```text
TASK:      P14-BOM-001
STATUS:    PASS, on a repeat gate that failed once and was re-run under control
BASELINE:  1c4b0bb (P14-ASM-001), clean tree, HEAD == origin/main
```

## TASK

List what an assembly drawing is made of, and label the components on the
drawing with the item numbers of that list — without ever storing a quantity
that could come to disagree with the assembly.

The milestone's own gate, from `TODO.md`:

```text
BOM contents correct
+ quantities correct
+ occurrence grouping correct
+ balloon mapping correct
+ configuration behavior correct
+ deterministic numbering
```

## BASELINE

`P14-ASM-001` at `1c4b0bb`, verified before anything was written: 1999/1999 on
`debug`, `release` and `debug-shared`, tree clean, `HEAD == origin/main`.

## SCOPE

### In

```text
a bill of materials, DERIVED from what an assembly view draws
grouping by part-definition identity, and by nothing else
quantity as the number of active occurrences
deterministic, contiguous item numbering
a BOM table annotation, drawn through the scene boundary
an item balloon annotation, whose number is a function of its occurrence
configuration and suppression, consumed from P13
persistence of the intent, and of nothing derived
```

### Out, and why

```text
frozen or hand-edited item numbers   would need stored intent, which would
                                     re-open the door to a stored quantity.
                                     ADR-022 records the refusal and what a
                                     later milestone would have to add
DESCRIPTION, MATERIAL, MASS columns  no document object carries them yet;
                                     inventing a metadata system would be a
                                     second part model
manual row ordering                  the ordering contract is part identity
balloon auto-placement, stacking,    no automatic placement anywhere in P14;
  or collision avoidance             P14-ANNO-001 recorded the same boundary
drawing commands / undo              P14-CMD-001
stable drawing references            P14-STREF-001
```

## THE BOM SEMANTIC MODEL

```text
assembly intent
  -> P13 solve / configuration / suppression
  -> drawnOccurrences(view)        the ACTIVE occurrence set (P14-ASM-001)
  -> group by ComponentDefinition::part
  -> sort rows by part ObjectId
  -> number 1..N over that order
  -> BomRow{item, part, name, occurrences}
```

Every step recomputed on every call.

## CANONICAL VS DERIVED

| | |
| --- | --- |
| **canonical** | that a BOM table exists, which view it lists, where it sits, its row height and column widths; that a balloon exists, which OCCURRENCE it points at, where it sits |
| **derived** | every row, every quantity, every item number, every cell of text, the table's lines, the balloon's number |

The file is asserted to contain no `"quantity"`, no `"item"` and no `"rows"`.
There is nowhere for a stale count to live.

## PART-DEFINITION GROUPING

Two occurrences are one row when `sameTarget(a.part, b.part)` — the
`ObjectReference` that `ComponentDefinition::part` already carries, compared on
owning document and object ID, deliberately ignoring the locator (ADR-003).

**Not** by display name, **not** by geometry, **not** by a shape hash, **not**
by traversal order. Two of those are asserted directly:

```text
two separately defined 20 mm cubes  -> TWO rows, and the test also asserts
                                       their bounding boxes match, so it is
                                       about identity and not about the parts
                                       differing
a part renamed to resemble another  -> still two rows, and the printed name
                                       follows the rename because it was
                                       never copied into the BOM
```

## OCCURRENCE PRESERVATION

A row carries `std::vector<ComponentId> occurrences`, not a count.
`quantity()` returns `occurrences.size()`, so the two cannot disagree — there
is no second field to fall out of step. `totalOccurrences()` across all rows is
asserted equal to the view's occurrence-set size, which would catch an
occurrence dropped or double-counted during grouping.

## QUANTITIES

```text
quantity(row) = number of ACTIVE occurrences grouped into it
```

```text
1, 2, ... 10 instances     asserted at every step, so a quantity taken from
                           the number of PARTS rather than occurrences fails
                           at the second
4, suppress one -> 3       and the suppressed one is asserted absent from the
                           occurrence list, not merely uncounted
last occurrence suppressed the row DISAPPEARS. A zero-quantity row is
                           unrepresentable: a row exists only because an
                           occurrence was seen
```

## ITEM NUMBERING AND ITS POLICY

```text
order    ascending part ObjectId
number   1..N over that order, contiguous
```

Stated explicitly, because the brief required the renumbering policy not to be
accidental: **numbering is compact, not retained.** Remove the last occurrence
of item 2 and the old item 3 becomes item 2. With nothing persisted there is
nowhere a retained number could live, and inventing one would be the stored-row
design ADR-022 rejects.

Ordering by part identity rather than by name or creation order is what makes
numbering independent of how the document was built. The test builds the same
assembly twice, creating the components in **opposite orders**, and requires
the same rows, quantities and item numbers.

## THE BOM TABLE

Semantic rows first, geometry second. The table draws through ADR-016's scene
boundary as `SceneLine`s and `SceneText`s, so a later PDF, SVG or DXF writer
transcribes and computes nothing.

```text
columns     ITEM | PART | QTY
placement   the TOP-LEFT corner; rows grow DOWNWARD, so adding a part
            lengthens the table away from where it was put rather than
            moving it (asserted)
sizes       paper millimetres. Default 15 + 60 + 15 mm across, 8 mm rows
lines       the boundary, one rule between each pair of rows, one between
            each pair of columns
text        numbers centred in their column, the part name reading from the
            left, as a name does
empty       an assembly with nothing active draws NO table rather than an
            empty box: a box with nothing in it says there is nothing to
            buy, which is a different claim
```

A BOM table is a sheet annotation and meets no `DrawingScale`: asserted 90 mm
wide and 16 mm tall at 1:1, 1:2 and 5:1.

## BALLOONS, AND THE MAPPING

```text
balloon -> occurrence -> part definition -> row -> item number
```

Each link is a qualified identity that already existed. The balloon stores the
first arrow and nothing else; the number is resolved at draw time.

```text
what it stores     the occurrence (an ObjectId naming a ComponentId), where
                   it sits, its text height
what it draws      a circle of 1.1 x the text height, the item number in the
                   middle, and P14-ANNO-001's leader to the occurrence
what it never      a number. There is no field for one
  stores
```

Its leader lands on the instance it labels: the anchor is the middle of the
part's bounding box **in the part's own space**, moved by that occurrence's own
solved transform. Two balloons on two instances of one part therefore show the
same number and point at different places — both asserted in one test.

## CONFIGURATION AND SUPPRESSION

```text
Config A   X x2, Y x1            -> 2 rows
Config B   X x1, Y suppressed,   -> 2 rows, Z is item 2 where in A there was
           Z x3                     no Z at all
back to A                        -> the WHOLE bill compares equal to the
                                    first, rows, quantities and numbers
```

A suppressed occurrence contributes nothing to any row, and a balloon on one
becomes **unresolved** rather than labelling a different instance of the same
part. That test is built so a rebinding balloon would succeed: the row still
exists, still item 1, because another occurrence of that part is still active.
Restoring the configuration makes the same balloon resolve again.

The same is asserted for a **deleted** occurrence, which is the other way one
can vanish.

## SAVE / LOAD

The real round trip: create, save, load, regenerate, compare.

```text
the definitions compare equal, table and balloon
the reloaded BOM compares equal to the original, whole
the reloaded table draws the same lines and the same text
the file contains no "quantity", no "item", no "rows"
```

And the regression this design exists for: **save at quantity 4, change the
model to 3, reload, require 3.** There is nowhere in the file for the 4 to have
been kept, and the test proves it rather than assuming it.

## MALFORMED STATE

```text
an unknown annotation type        refused: "unknown annotation type"
a row height of zero or negative  refused: "greater than zero"
a column width that is not a      refused: "expected a number"
  number
a balloon naming an object that   LOADS, and is UNRESOLVED when drawn
  does not exist                  ("does not exist")
```

That last one is the established contract rather than an oversight, and is
recorded as such: every drawing object in this codebase is read definition by
definition, because the object a reference names may not have been read yet. A
reference is therefore resolved when it is USED. What the test requires is that
no number appears anyway.

## FAILURE PATHS

```text
a BOM of a view that draws one object   "draws one object rather than the
                                        assembly"
a BOM of an assembly with nothing       "no active components"
  active
an occurrence whose part was deleted    "not an object of this document"
a balloon that names a plane            refused when made: "must name one"
a balloon that names a non-component    unresolved: "not a hole or a
                                        component"
a table that points at something        refused: "points at nothing"
a table or balloon carrying words       refused: "takes its words from the
                                        model"
a balloon with no solved transform      "did not solve"
```

## A DELIBERATE SPLIT: THE BOM SURVIVES A FAILED SOLVE

A bill of materials says WHAT is in the assembly. That does not depend on WHERE
the solver put anything, so a BOM and its table are still correct when the
solve fails — they are not stale, they are the current active set. A balloon is
the opposite: it needs a transform to put its leader on an instance, so it
fails by name.

Both halves are asserted in one test, because the pair is the contract.

## DETERMINISM

```text
computed six times           identical bill, identical table lines and text,
                             identical balloon number
row order                    ascending part ObjectId
occurrences within a row     ascending ComponentId
insertion order              two documents built in opposite orders give the
                             same rows, quantities and numbers
no unordered container, no pointer order, no name-dependent ordering, no
clock, no seed
```

## INDEPENDENT FIXTURES

Every expected value is written out in the test.

```text
Fixture A   A x3, B x2, C x1        -> 3 rows, quantities 3, 2, 1;
                                       totalOccurrences 6
Fixture B   4 of one part, one      -> quantity 3, and the suppressed one is
            suppressed                 absent from the occurrence list
Fixture C   two separately defined  -> 2 rows, with their bounding boxes
            20 mm cubes                asserted equal
Fixture D   4 occurrences of one    -> one row, quantity 4; balloons on the
            part                       first and last both read "1" and point
                                       at different places
table       1 part, default style   -> 90 mm x 16 mm, 6 texts, 5 lines
            2 parts                 -> 9 texts
```

## ADVERSARIAL REVIEW

One production defect, three fixture errors of mine, and two process findings.

### 1. A BOM table that pointed at something said it was a note

`validate()` refused a no-target annotation carrying a target with the words
*"a note is placed on the sheet and points at nothing"* — whatever kind had
actually been made. Until this milestone that was true, because a note was the
only kind that points at nothing; a BOM table is the second, so the message
now sent a reader looking for a note they had never created.

```text
found by   writing the refusal test and reading the message it produced,
           rather than only checking that it refused
fixed      the message names the kind: "a bom_table annotation is placed on
           the sheet and points at nothing"
test       the refusal test asserts the kind is named, and the P14-ANNO-001
           tests still pass unchanged because the wording they match on is
           still there
```

Small, but it is the class of defect this repository treats as real: a
diagnostic that says what did not happen.

### 2. Three fixture errors

```text
"must be a number"      the reader says "expected a number". The test was
                        asserting a message that does not exist, which
                        passes only when nothing is checked
9 texts, not 12         a table with a header and two rows has three lines
                        of three cells. The expectation was arithmetic done
                        carelessly, and the code was right
rejection on load       the test required a balloon naming a missing object
                        to be REFUSED when the file loads. It is not, and
                        that is the established contract: every drawing
                        object here is read definition by definition, because
                        the object a reference names may not have been read
                        yet. The test now asserts what must actually hold --
                        the file loads and the balloon is UNRESOLVED when
                        drawn, with no number appearing
```

### 3. Two process findings

```text
a build gate that was   `cmake --build ... | tail -2 && echo "BUILD OK"`
not a gate              takes its exit status from `tail`, which always
                        succeeds. The build failed, "BUILD OK" printed, and
                        the tests ran against the previous binary and passed.
                        This is the same trap P14-ASM-001 recorded, wearing a
                        different disguise. The build's own status is now
                        captured directly and the tests run only when it is 0
a non-result counted    `cmd.exe /c "chcp 65001 > nul && ctest --preset
as a pass               debug"` invoked from Bash opened an interactive shell
                        and exited. CTEST_RC was 0 and ctest had never run.
                        Caught by looking at the log, which held three lines
                        and a Windows banner. Re-run through PowerShell with
                        an explicit working directory, which is the run that
                        counts. This is P14-DIM-001's Run 2 again, and the
                        rule it produced -- a result is only a result if the
                        log shows the work -- is what caught it
```

### What was attacked and held

| Question | Answer |
| --- | --- |
| Can two identical occurrences become two rows? | No. Grouped by `sameTarget` on the part reference; four occurrences of one part are one row of quantity 4 |
| Can two distinct part definitions be grouped? | No. Two separately defined 20 mm cubes give two rows, and the test asserts their bounding boxes are equal so it is about identity |
| Can quantity come from the part count rather than the occurrence count? | No. `quantity()` IS `occurrences.size()`, and the test walks 1 to 10 |
| Can a suppressed component remain in a quantity? | No, and it is asserted absent from the occurrence list rather than merely uncounted |
| Can a suppressed last occurrence leave a zero-quantity row? | No. A row exists only because an occurrence was seen, so zero is unrepresentable |
| Can configuration switching leave stale rows? | No. A then B then A compares the whole bill equal |
| Can numbering change because a container's order changed? | No unordered container is used; rows sort by part ObjectId. Two documents built in opposite orders give the same numbers |
| Can save/load renumber an identical BOM? | No — the reloaded bill compares equal, whole |
| Can one item number reach two rows? | Numbers are 1..N over sorted rows; asserted contiguous and unique |
| Can a balloon store a number and go stale? | There is no field for one. Asserted by changing the assembly under a balloon |
| Can a balloon rebind to another identical occurrence? | No — asserted in the case where it *could*: the row still exists and is still item 1, and the balloon still fails |
| Can deleting an occurrence retarget a balloon? | No — separate test, because deletion is not suppression |
| Can two balloons on one part disagree? | No — both read 1, while pointing at different places |
| Can row order differ between Debug and Release? | Ordered by ObjectId; the three-preset qualification covers it |
| Can the view scale resize the table or a balloon? | No — 90 x 16 mm at three scales, balloon diameter fixed at four |
| Can a broken solve publish a stale BOM? | The BOM is not stale — it is the current active set, and does not depend on the solve. Stated as a deliberate split and asserted with the balloon, which does fail |
| Can geometry equality replace part identity? | No — Fixture C |
| Can display names become identity? | No — a part is renamed mid-test and the rows do not merge |
| Did this start P14-STREF-001 or P14-REGEN-001? | No new reference kind and no regeneration mechanism; the balloon uses `ComponentId`, which P14-ASM-001 already proved |

## TESTS

**31 new test cases**, all in `tests/drawing/BomTests.cpp`.

```text
[bom]        1287 assertions in  31 test cases
[drawing]    8836 assertions in 332 test cases   (was 7549 in 301)
```

Grouped:

```text
contents       three rows of 3, 2, 1; quantity at 1..10; totalOccurrences
grouping       identical geometry, separate definitions -> two rows; a
               rename does not merge them
occurrences    a row keeps its ComponentIds, sorted
numbering      unique, contiguous, ascending part identity; unchanged when
               the components are created in the opposite order; compact
               when a row goes
suppression    exact decrement; the last occurrence removes the row; no
               zero-quantity row can exist
configuration  A -> B -> A, the whole bill equal
balloons       the number of the occurrence it points at; four occurrences
               of one part all read the same and point at different places;
               the number follows the assembly; unresolved on a suppressed
               target and on a deleted one, with the row still present so a
               rebinding balloon would have succeeded; paper-sized at four
               scales
table          header and one row per part, cell by cell; 90 x 16 mm at
               three scales; grows downward; no header; empty assembly
persistence    the round trip; no quantity, item or rows in the file; a
               saved 4 cannot override a current 3; five malformed files
failure paths  a BOM of an object view; a part deleted under an occurrence;
               a table that points; words on a derived kind; a balloon with
               no solved transform
determinism    six computations, identical bill, table and number
```

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2030 / 2030** (280.43 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2030 / 2030** (483.48 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2030 / 2030** (348.20 s) |

Baseline was 1999 tests; this milestone adds **31**.

### THE REPEAT GATE FAILED, AND THE HARNESS SAID SO

```text
repeat release  exit 0   2029 / 2029, each test five times (1359.61 s)
repeat debug    exit 8   99% passed, 11 failed
qualification finished, 1 stage(s) failed
```

The harness propagated it, as `P14-DIM-001`'s exit-code fix requires. The
failed logs are kept as `ctest-repeat-debug-failed.log` and
`qualification-times-failed.txt` and are **not** erased.

**Neither failure is in this milestone's work, and both are environmental.**

```text
cli.assembly.batch      "cannot replace '.../built.bcad': Permission denied"
                        THE THIRD OCCURRENCE of the OneDrive fault: it failed
                        P14-DIM-001's debug repeat and P14-ANNO-001's release
                        repeat, and it failed here after PASSING THREE TIMES
                        in the same run before the fourth repeat. Nine other
                        cli.assembly tests then report "Not Run", because
                        they share the fixture this one writes
compile_fail.units.     terminated by TIMEOUT at 64.59 s, expecting exit 1.
  assign-mass-to-length A compile-failure test invokes the compiler; under
                        --repeat until-fail:5 with -j 8 there are eight
                        compilations plus the whole suite competing, and this
                        one did not finish in time. It is load, not a defect
```

### One controlled rerun, and what it showed

Exactly one, as the standing direction for this gate requires — not repeated
attempts until green:

```text
ctest --preset debug -j 8 -R "<the repeat filter>" --repeat until-fail:5
  -> 100% tests passed out of 2029
  -> RERUN_EXIT=0                         (910.07 s)
  -> ctest-repeat-debug-rerun.log
```

The tree was not touched between the two runs: the tree IDs below are the same
ones the harness recorded before the first build, and the only files written in
between were logs under `docs/`.

So the determinism gate passes on the rerun, and the failure it recovered from
is an intermittent fault in the filesystem the build tree sits on — now seen in
**three** milestones and **both** repeat presets. `TODO.md` carries the decision
to move build output off OneDrive, and this run raises its priority rather than
closing it.

### The harness itself

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it gives `QUALIFICATION FAILED: 3 stage(s) failed`
and exit 3.

### One qualification was voided, and one result was not a result

```text
voided     a first qualification was started and stopped minutes in, because
           the misleading-diagnostic fix below changed the tree while it ran.
           The tree was re-frozen and the qualification run again from clean
non-result a Debug regression reported CTEST_RC=0 having never run ctest:
           `cmd.exe /c "chcp 65001 > nul && ctest ..."` invoked from Bash
           opened an interactive shell and exited. The log held three lines
           and a Windows banner. It was re-run through PowerShell -- 2030/2030
           -- and the first is recorded as a non-result, not a pass
```

### The qualified tree is the committed tree

Tree IDs from a scratch index, taken three times — before the first build, by
the harness after the last test run, and again before the commit — identical in
all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           e2f38d042907dfeff95e659909f88eed43f2244d
src               94a43e3c8a262776c725f61146ed9adb4552b507
tests             bb0bcba9e11248101ba083016132c33595945494
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-ASM-001 baseline**: no CLI was touched and the
build configuration was not altered to obtain a pass.

## KNOWN LIMITATIONS

```text
1  No frozen or hand-edited item numbers, and no manual row order. Numbering
   is a function of the assembly (ADR-022). A milestone that needs a retained
   number has to add stored intent and say so.
2  Three columns: ITEM, PART, QTY. No DESCRIPTION, MATERIAL or MASS, because
   no document object carries those yet and inventing a metadata system would
   be a second part model.
3  The BOM is recomputed on every call, and drawing N balloons computes it N
   times. Nothing caches it, for the reason ADR-011 and ADR-014 give.
4  A part in another document is counted and grouped correctly but is named
   by its ObjectId, because naming it would need the implicit filesystem
   access ADR-003 forbids.
5  Text width is not measured -- nothing here knows a font -- so a long part
   name can overrun its column. Column widths are the engineer's to set.
6  No balloon auto-placement, stacking or collision avoidance, and no leader
   to more than one occurrence. P14-ANNO-001 recorded the same boundary.
7  A balloon naming a non-existent occurrence loads and is unresolved when
   drawn, rather than being refused on load. That is how every drawing
   reference in this codebase behaves, for load-ordering reasons, and is
   asserted rather than assumed.
```

## RESULT

```text
TASK:            P14-BOM-001 -- Bill of materials and balloons
IMPLEMENTATION:  ADR-022; billOfMaterials() and itemNumberOf(), both derived
                 and stored nowhere; BomRow carrying its occurrences rather
                 than a count; grouping by ComponentDefinition::part;
                 ordering and numbering by part identity; AnnotationType::
                 BomTable and ::Balloon as annotation kinds, with the table
                 drawn through ADR-016's scene boundary and the balloon's
                 number resolved from its occurrence at draw time; a
                 component occurrence made a resolvable annotation target
TESTS:           31 new; 2030/2030 in debug, release and debug-shared, each
                 from clean; 2029/2029 five times over in release, and in
                 debug on a controlled rerun after an environmental failure
VALIDATION:      3/2/1 quantities, 1..10 instances, two identical-geometry
                 parts as two rows, a 90 x 16 mm table, balloons on four
                 instances of one part -- every figure written out in the
                 test
ADVERSARIAL:     1 production defect found and fixed, 3 fixture errors
                 corrected, 2 process findings recorded
WARNINGS:        0 in all three builds
DETERMINISM:     computed six times identically; numbering independent of
                 component creation order; repeat gate clean on the rerun
RESULT:          PASS, on a repeat gate that failed once environmentally and
                 passed on one controlled rerun
EVIDENCE:        this directory
TODO:            P14-BOM-001 -> [x]
CARRIED OPEN:    none
OPEN DECISION:   move build and test output off OneDrive. The fault recurred
                 here for the THIRD time, in a third milestone. This run
                 raises its priority rather than closing it.
NEXT:            P14-STREF-001 -- stable drawing-to-model references
```

## FILES

```text
qualification/qualify.cmd                       the harness, from P14-ASM-001
qualification/verify-harness.cmd                its exit-code regression; run and passing
qualification/run-qualification.cmd             the entry point and its repeat selection
qualification/qualification-times.txt           every stage and exit code, and the tree IDs
qualification/qualification-times-failed.txt    the FAILED run's stage exits, kept
qualification/ctest-repeat-debug-failed.log     the Permission denied and the timeout, as found
qualification/ctest-repeat-debug-rerun.log      the one controlled rerun: 2029/2029, exit 0
qualification/build-*.log                       three presets, 0 warnings each
qualification/rebuild-*.log                     the fresh-binary proof
qualification/ctest-*.log                       2030/2030 in each preset
```
