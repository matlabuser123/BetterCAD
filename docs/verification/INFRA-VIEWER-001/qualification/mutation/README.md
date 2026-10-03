# INFRA-VIEWER-001 — mutation testing

```text
SUBJECT:  src/renderer/occt/OcctViewer.cpp -- the only file in the project
          that sees OCCT's visualization layer
METHOD:   introduce one plausible mistake, rebuild, run the [renderer] suite,
          restore. Fifteen times.
RESULT:   see results.txt
```

**A mutation that SURVIVES marks a gap in the tests, not a harmless variant.**
That is the whole point of the exercise, and this milestone's run produced two
such gaps -- Findings 4 and 5 in
[../../ADVERSARIAL_REVIEW.md](../../ADVERSARIAL_REVIEW.md).

## Why a viewer needs this more than most code

A renderer is unusually easy to test badly. `ToPixMap` returns `true` for a view
that drew nothing; `DisplayedObject::visible` can be bookkept perfectly while
the wrong thing is on screen; a pick can return the right answer by accident
when only one object is displayed. Every one of those is a test that passes for
the wrong reason, and none of them is visible by reading the test.

Mutation is the check on the tests rather than on the code: if the adapter can
be broken in an obvious way and the suite still passes, the suite is not
measuring what it claims to.

## Running it

```text
bash mutate.sh
```

**Nothing else may touch the source tree while it runs, and stopping it means
killing the process tree.** This is not fussiness; it cost this milestone an
afternoon. The harness restores every file from its snapshot between
mutations and again from its `EXIT` trap, so an edit made during a run is
reverted without a word, and a build of a half-edited tree reports "killed by
the COMPILER" for a mutation that was never the problem. One interrupted run
here produced a full, plausible, **entirely void** `results.txt`, a 0-byte
test executable -- the trap's rebuild and an interactive build writing it at
once -- and a silently reverted production fix. A `mutate.lock` now refuses a
second concurrent run; it cannot refuse a careless edit.

It needs `BETTERCAD_BUILD_ROOT` and an already-configured `debug-ext` build
(it builds the `bettercad_tests` target only, not the whole project). Results
are appended to `results.txt` as each mutation finishes, so a long run can be
watched.

```text
python apply.py snapshot   take the pristine copies
python apply.py <index>    apply that mutation to the PRISTINE text
python apply.py restore    put every pristine text back
python apply.py count      how many mutations there are
python apply.py label <i>  that mutation's label
```

Three properties of the harness are deliberate:

```text
MUTATIONS CANNOT COMPOUND
    Every file is restored from the pristine copy before the next mutation is
    applied, so the only difference from the committed tree is the one
    mutation under test.

A PATTERN THAT IS ABSENT OR AMBIGUOUS IS REFUSED
    apply.py counts the occurrences and exits non-zero on 0 or more than 1,
    and the harness records NOT APPLIED. A mutation that landed somewhere
    unintended would make its verdict meaningless, and a mutation whose
    pattern silently stopped matching -- after a refactor, say -- would report
    a false kill forever.

THE TREE IS RESTORED EVEN ON FAILURE
    `trap restore EXIT`, followed by a rebuild. A mutation left in the tree
    would be committed.
```

## Reading a verdict

```text
killed (assertions: N | P passed | F failed)
    A TEST caught it. This is the result that counts, and the assertion
    counts are recorded so a kill can be traced to a test rather than taken
    on trust.

killed by the COMPILER
    WEAKER EVIDENCE, and recorded as such: it shows the mutant would not
    build, NOT that any test would have noticed it. One mutation here first
    came back this way and was rewritten until it compiled -- Finding 5.

*** SURVIVED ***
    A GAP IN THE TESTS. Finding 4.

NOT APPLIED (PATTERN-ABSENT | PATTERN-AMBIGUOUS xN)
    The mutation did not land. Not a result at all; fix the pattern.
```

## What the fifteen mutations attack

```text
 1  the display mode is not set on the presentation      (Finding 1's defect)
 2  the view size is checked only after a window is      (Finding 2's defect)
    built from it
 3  a pick returns the first presentation instead of     (Finding 3's gap)
    the one detected
 4  selection reports every displayed object rather than the selected ones
 5  hiding does not record that the presentation is hidden
 6  hiding erases nothing, so a hidden body stays drawn
 7  a nonsensical zoom is passed to the kernel instead of ignored
 8  resize does not record the new size, so renders keep the old one
 9  an empty body is displayed as an empty presentation instead of refused
10  removing a presentation leaves it in the viewer's own list
11  the background colour is recorded but never given to the view
12  coverage ignores its tolerance and counts every       (Finding 5)
    pixel as different
13  sampling out of bounds returns a neighbouring pixel    (Finding 4's gap)
    instead of refusing
14  a standard view is set but the camera never refits
15  resize records the new size but never resizes the        (Finding 6's
    window it owns                                            defect)
```

The numbers are positions in `mutations.json`, which is the order `results.txt`
reports them in.

Mutations 1, 2, 3, 13 and 15 each reinstate a defect or a test gap this review
found, so a regression that reintroduced one would be caught by the harness and
not only by the suite.

**Finding 7, the Qt logical-versus-device conversion, has no mutation here, and
that is a limitation rather than an omission.** It lives in
`apps/bettercad/ViewportWidget.cpp`, which the test executable does not link;
a mutation there could not be killed by the `[renderer]` suite, and a mutation
that no test can kill teaches nothing. What covers that conversion is the
measurement the GUI smoke test prints.

## This run

`results.txt` is the run against the FINAL tree. An earlier run, against the
tree before Findings 4 and 5 were fixed, is what produced them: mutation 12
survived and mutation 11 was killed only by the compiler. Re-running the whole
set after changing the tests is not ceremony -- a kill recorded against a
superseded tree is not evidence about the tree being committed.
