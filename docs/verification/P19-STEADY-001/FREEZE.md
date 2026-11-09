# P19-STEADY-001 — the qualification freeze

Written before the gate was launched. Everything here is a measurement taken
against the tree as frozen, not a prediction about it.

**THIS IS THE SECOND FREEZE OF THIS MILESTONE.** The first was voided by its own
qualification run, which failed one gate. What that was, and what it cost, is the
last section of this file; everything above it was re-measured or re-read against
the second tree rather than carried over.

## The frozen surface

Eight paths, the same eight every milestone in this lineage freezes:
`apps include src tests examples cmake CMakeLists.txt CMakePresets.json`.
`TODO.md`, `ROADMAP.md` and everything under `docs/` are OUTSIDE it, because
none of them can affect an executable or a test.

```text
HEAD at the freeze   8f1c59544385b237fd57089a103d41f7dfb212c4

apps               fc398a3d2e524178cf38e8b667236379d8381086   same
include            588dd50bf9a82cb58b3a93ccdcfe752fcdf896df   CHANGED
src                9fce91ebc86a4095fcc24e821bb86a793d835650   CHANGED
tests              f3eb700ce67c512b15001b75471cba873a12cc55   CHANGED
                   was 9d12c8ff496e978e6bbc0833de402330e34d1d22 at the FIRST
                   freeze, which the gate VOIDED -- see "The first run voided
                   this freeze" at the end of this file
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   same
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   same
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   same
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   same
```

"same" means bit-identical to `P19-LINSOLVE-001`'s frozen tree. Read with a
scratch `GIT_INDEX_FILE`, `git read-tree HEAD`, `git add -A` over the eight, then
`git write-tree --prefix=` for each directory and `git ls-files -s` for the two
files — never `git rev-parse HEAD:<path>`, which would read the committed tree
rather than the working one.

## What moved

```text
A  include/bettercad/cfd/CfdSteady.hpp
A  src/cfd/CfdSteady.cpp
A  tests/cfd/CfdSteadyTests.cpp
A  tests/compile_fail/CfdSteadyMisuse.cpp
M  include/bettercad/cfd/CfdResult.hpp      <- P19-DATA-001's QUALIFIED TYPE
M  src/cfd/CfdResult.cpp                    <- P19-DATA-001's QUALIFIED TYPE
M  src/cfd/CMakeLists.txt                   one source added
M  tests/CMakeLists.txt                     one suite added
M  tests/compile_fail/CMakeLists.txt        one group, 14 cases
M  tests/cfd/CfdDataTests.cpp               14 create() call sites, the mirror
M  tests/cfd/CfdFluidTests.cpp              1 create() call site
```

**TWO OF THE ELEVEN ARE A QUALIFIED TYPE, AND THAT IS THE MILESTONE'S LARGEST
RISK.** `CfdResult` gains its field arrays (`ADR-077` Decision 2), so
`CfdResult::create`'s signature changes and every caller moves. There was no
production caller — `CfdResultSlot` had no production consumer in the tree until
this milestone — so all 14 are in tests, counted rather than estimated.

The mirror `CfdData_AResultHoldsProvenanceAndMetadataOnly` is what **stopped the
build** until the change was stated, which is exactly the job `P19-DATA-001`
built it for. It is extended and renamed to
`...ProvenanceMetadataAndItsFields`, and the old name's disappearance is the
statement.

## The three figures, counted off the frozen tree

Counted by `ctest -N` against this tree, not predicted. `P19-GEOM-001`'s freeze
recorded getting that wrong once, and `P19-LINSOLVE-001`'s post-gate
reconciliation found a `cfd` assertion figure four short that was INTERNALLY
CONSISTENT and therefore unfalsifiable from inside its own document.

```text
full suite, per preset      6903  = 6870 (P19-LINSOLVE-001)
                                    + 19 unit.CfdSteady_
                                    + 14 compile_fail.cfdsteady
repeat set, per preset      3493  = 3474 (P19-LINSOLVE-001) + 19 matching the
                                    filter; compile_fail is excluded by design
repeat EXECUTIONS per log  17465  = 3493 x 5
```

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT PROGRESS LINES.** `ctest --repeat
until-fail:5` prints one `Start` marker and one `Test #N: ... Passed` line per
EXECUTION, and prefixes only the LAST execution of each test with a `k/3493`
progress counter. So a repeat log holds 17,465 of each of the first two and
3,493 of the third, and the ratio that must be exactly 5.000 is markers to
progress-prefixed lines.

A first pass at `P19-LINSOLVE-001`'s reconciliation counted 999 progress lines in
a 6870-test log, which is `1..999`: the regex required leading whitespace and
`ctest` right-pads the counter to the width of the total, so a four-digit counter
has none. 999 is not a measurement of anything.

## The subsystem arithmetic, which a passing suite cannot make

```text
                   before (8f1c595)       at the freeze          delta
cfd                751,771 in 644         753,230 in 663         1,459 and 19
linalg               2,560 in  29           2,560 in  29             0 and 0
structural         271,414 in 415         271,414 in 415             0 and 0
thermal            595,622 in 1168        595,622 in 1168            0 and 0
```

The `cfd` delta is 1,459 where the new suite holds 1,263. The other 196 are in
`CfdDataTests.cpp` and `CfdFluidTests.cpp`, whose fixtures gained a `layout()`
helper and its `REQUIRE`s when `create`'s signature changed. 1,263 + 196 = 1,459.

The last three lines matter because this milestone modifies `CfdResult`, which
`cfd` alone uses — unlike `P19-LINSOLVE-001`, which modified a `linalg` kernel
that `structural` and `thermal` both link. A deleted test and an added test net
to nothing in a pass/fail count; they do not net to nothing here.

`unit\.Structural` and `unit\.Thermal` stay in the repeat filter even so. They
are not REQUIRED here the way they were for `P19-LINSOLVE-001`, and removing a
preset's coverage to save time is how a regression gets missed.

## Pre-freeze state of the gates

**EVERY LINE BELOW WAS MEASURED AFTER THE LAST EDIT TO THE FIRST FREEZE'S
FROZEN SURFACE, AND THE SECOND FREEZE HAS ONE EDIT AFTER THEM.** That claim held
without qualification for the first freeze. The repair described in this file's
last section then changed `tests/compile_fail/CfdIdMisuse.cpp`, so the following
lines were NOT all re-measured against the second freeze, and saying they were
would be the easiest false claim in this document to make.

What WAS measured after that edit:

```text
the repaired gate         PASSES, diagnostic `is private within this context`
the cfdids control        compiles cleanly, exit 0 -- so every failure in the
                          file is still attributable to its own line
cfdids + cfdsteady whole  42 of 42
```

What was NOT re-measured, and why it does not need to be: the file is compiled
only as that group's control and as its own cases, so it reaches no unit-test
binary — `[cfd][steady]`'s 1,263 assertions in 19 cases cannot move — and the
three caches were not touched. **No build was run after the gate was launched**,
because the harness owns the three build trees while it is running and a build
of mine would corrupt the thing being measured.

The second gate re-runs all three presets from clean, so its own logs supersede
every line below in any case. This block exists to catch a problem BEFORE three
and a half hours are spent, not to stand in for the run.

```text
debug-ext            built, 0 error:, 0 warning: under -Werror
release-ext          built, 0 error:, 0 warning:
debug-shared-ext     built, 0 error:, 0 warning:
[cfd][steady]        1,263 assertions in 19 cases, IDENTICAL in all three
architecture         205 of 205, including architecture.layering
compile_fail         14 of 14, the cfdsteady group
caches               all three: CMAKE_COMMAND under the WinLibs package path,
                     0 msys64 references, the pinned deps prefix
```

### The compile-fail group's first run caught a defect in itself

13 of 14 passed. The fourteenth,
`compile_fail.cfdsteady.there-is-a-steady-solve-input-type`, **failed to compile
correctly** — exit 1 as expected — and was only mis-ATTRIBUTED: GCC says
`'CfdSteadySolveInput' is not a member of 'cfd'` and the regex group it was
placed in did not list that wording. That is the `P19-GRAD-001` lesson the
group's own comment cites, and it cost six of twenty-eight gates on
`P19-LINSOLVE-001`'s first run. Caught here by running the gates rather than by
trusting the list; it now has its own regex.

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-LINSOLVE-001`'s, **verified by hash rather than asserted** (raw `sha1sum`):

```text
qualify.cmd            244b38de8feac0475af7cdc56ba9415d27d8dd29   IDENTICAL
verify-harness.cmd     afd0197d4f4726db7f533c2f27f4f6e069ba83c6   IDENTICAL
launch.cmd             cf6c1f112d71586098a78652091d0375c8d04051   IDENTICAL
preflight-space.cmd    59910328ab43e07b9e0c474f969994ad483d3f68   IDENTICAL
preflight-space.ps1    b76d9325e8131c265443540a9ac914e236731a66   IDENTICAL
run-qualification.cmd  git hash-object f8b90f59cc729226a3ba15a8718fc979387ba561
```

**THE TWO HASHING CONVENTIONS MUST NOT BE MIXED.** The five `IDENTICAL` values
are raw `sha1sum` of the bytes; `run-qualification.cmd`'s is `git hash-object`,
which prepends a blob header. They never agree for the same file, and both are
labelled wherever they appear.

`run-qualification.cmd` is this milestone's own, and its **sixteen executable
statements were extracted from `P19-LINSOLVE-001`'s copy and compared back
against the source's** rather than retyped: identical, in order. All 95 non-blank
lines pass the whole-word prefix check (`rem`, `set`, `call`, `echo`, `setlocal`,
`endlocal`, `@echo` followed by whitespace or end-of-line) — the check that
exists because `P19-ARCH-001`'s copy carried a line beginning `remrem` which a
prefix match let through.

### An inherited defect in `verify-harness.cmd`, carried forward unfixed

```text
line 41  if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%<0x08>c-build"
```

A literal **backspace byte** where `\bc-build` was meant, in 72 of 96 committed
copies, entered 2026-09-30 with the P15 materials work. It fires only when
`BETTERCAD_BUILD_ROOT` is undefined, which `run-qualification.cmd` always
defines, so no gate in any milestone has ever evaluated it. Found during
`P19-LINSOLVE-001`'s post-gate reconciliation, reported to the owner as a scope
decision, and **not fixed** — repairing it across the lineage is not authorized
by this checklist either. The copy here is byte-identical to the lineage's,
defect included, so that nothing about this milestone's harness is novel.

## The reviews and the probes, both before this freeze

```text
reviews    2 independent, READ-ONLY, neither shown the other's output
           1 DEFECT (gravity never reached the momentum equation), fixed with a
             regression test VERIFIED TO FAIL WITHOUT THE FIX
           8 RISKs recorded as ADR-077 L9 to L14, L16, L17
           7 NITs, 5 acted on
probes     3 rounds, 20 distinct behavioural probes, 17 KILLED
           3 surviving BY DESIGN: L15, L16, L17, each with the reason a test
             cannot reach it
           2 no-op controls, SURVIVED every round they ran
           2 of the author's own predictions wrong, both recorded
```

## What the verdict is, and what it is not

**The verdict is the harness's own two statements**: the sentence in
`run-qualification.out` and the failed-stage count in
`qualification-times.txt`. NOT the launcher's exit code. `P19-MOM-001`'s first
attempt was reported as "completed (exit code 0)" while the harness itself had
written `QUALIFICATION FAILED: 2 stage(s) failed`.

`P19-LINSOLVE-001`'s reconciliation also found that a `grep` for the verdict
returned nothing because it assumed upper-case wording; the file is read whole.

## What is reconciled after the run, and how

```text
17 stages                  every one exit 0 in qualification-times.txt
6903 per single-run log    Start markers = result lines = progress-prefixed
                           = Passed, and 0 failure markers
17465 per repeat log       Start = result lines = Passed, 3493 progress-prefixed,
                           ratio EXACTLY 5.000
0 error: / 0 warning:      in all three build logs; the only substring hit is
                           the filename Error.cpp.obj
the eight identities       read FIVE times and equal in all five: by the
                           harness before its first build and after its last
                           test, recomputed from the working tree, off the
                           staged tree, and OUT OF the commit object of
                           370b90da75de236e66d0ddfbda1fb9254e557133. git rev-parse HEAD:<path> is
                           used only for the last one
freshness                  no ninja-owned artifact newer than its build exit.
                           Asked of `ninja -t targets all`, not of file
                           extensions: P19-LINSOLVE-001's first attempt at this
                           flagged 24 zero-byte stubs the deployment TESTS write
                           into their own ctest scratch directories
caches                     re-verified, CMAKE_COMMAND included
assertion identity         read FROM THE THREE QUALIFIED BINARIES. ctest
                           captures no stdout from a passing test
```

**NO BUILD IS RUN DURING THE RECONCILIATION.** A relink would move the binaries'
mtimes and destroy the evidence being checked. That is `P19-LINSOLVE-001`'s `r20`
failure, where a 40-minute full-suite run was invalidated by the author's own
rebuild, and the way not to repeat it is not to rebuild.

## What is not frozen

`TODO.md`, `ROADMAP.md`, `docs/architecture/decisions/ADR-077*.md` and everything
under `docs/verification/P19-STEADY-001/`. None can affect an executable or a
test. If any of the eight paths above changes after this record, the
qualification is void and is run again from clean.

## The first run voided this freeze

The first gate ran to completion over three and a half hours and **failed**:

```text
run-qualification.out   QUALIFICATION FAILED: 3 stage(s) failed.
qualification-times.txt debug-ext        ctest exit 8
                        release-ext      ctest exit 8
                        debug-shared-ext ctest exit 8
                        the two repeat runs    exit 0
the launcher            reported "completed (exit code 0)"
```

**THE LAUNCHER SAID EXIT 0 AND THE HARNESS SAID FAILED**, which is why this file
says the verdict is the harness's two statements. That rule was written for
`P19-MOM-001`'s first attempt and it earned its keep again here.

One test failed, the same one in all three presets, out of 6903:

```text
compile_fail.cfdids.a-result-cannot-be-built-without-its-factory
```

### It failed for the reason that matters, not the reason that is convenient

The gate builds one target and requires exit 1 AND a diagnostic matching
`is private within this context|private`. The build exited 1, as expected. The
diagnostic was not an access error:

```text
error: no matching function for call to CfdResult::CfdResult(<brace-enclosed
       initializer list>)
note:  candidate 1: CfdResult(CfdResultSource, CfdResultMetadata, CfdResultFields)
note:  candidate expects 3 arguments, 2 provided
```

The case read `CfdResult conjured{CfdResultSource{}, CfdResultMetadata{}}`, which
matched the private two-argument constructor until `ADR-077` Decision 2 gave it a
third parameter. GCC resolves arity before it reports access, so the case became
an ARITY test wearing an access test's name. **The compile still failed and the
gate still exited 1; the claim it exists to make had silently stopped being
tested.** A gate that cannot fail for its own reason is not a gate.

Nothing was wrong with the production code. The private constructor is still
private and `create` is still the only way to a `CfdResult`. What this milestone
broke was the PROOF of that, and the proof is the thing the gate holds.

### The repair, and the repair that was refused

```text
REFUSED   widen the regex to accept "no matching function"
          -- the gate would pass and would no longer test access at all.
             CLAUDE.md: never relax a gate to obtain a PASS.
REFUSED   add a second case for the access half
          -- the arity half is already covered by this milestone's own
             compile_fail.cfdsteady.create-still-takes-only-source-mesh-and-
             metadata, and it would move the test count for no new claim.
DONE      pass the right arity, so overload resolution reaches the private
          constructor and the diagnostic is an access error again:
          {CfdResultSource{}, CfdResultMetadata{}, result.fields()}
```

The third argument comes from a real result, because `CfdResultFields` cannot be
conjured either. So the gate now says something STRONGER than it did before the
milestone: a consumer holding a legitimate field set, taken from a published
result, still cannot assemble a `CfdResult` around it.

Measured after the repair, not predicted:

```text
the gate            error: CfdResult::CfdResult(CfdResultSource,
                    CfdResultMetadata, CfdResultFields) is private within
                    this context                                    PASSES
the group control   compiles cleanly, so every failure in the file stays
                    attributable to its own line                    exit 0
both groups whole   compile_fail.cfdids + compile_fail.cfdsteady     42 of 42
```

### What the blast radius missed, and why

This file's "What moved" section lists eleven paths and counted fourteen
`create` call sites. `tests/compile_fail/CfdIdMisuse.cpp` is not among them
because **it never calls `create`** -- it names the CONSTRUCTOR, and the search
that found the callers was a search for the factory. The blast radius was drawn
around the function whose signature changed and not around the type, and a
private constructor has exactly one deliberate caller plus one deliberate
non-caller. The non-caller is a gate.

The lesson is narrow enough to state: when a change alters a constructor's
PARAMETER LIST, the blast radius includes every compile-fail case that names
that constructor, and those cases are invisible to a search for the factory.

### The cost

The whole gate is re-run from clean. Three presets built from scratch and two
repeat sets, about three and a half hours, because a test file inside the frozen
surface changed and this file's own rule says the qualification is then void.
Only `tests` moved; `include` and `src` are bit-identical to the first freeze,
which is the check that the repair touched no production code.
