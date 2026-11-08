# P19-MOM-001 — qualification attempt 1, FAILED

```text
launched    2026-11-08 08:32:47
finished    2026-11-08 11:03:27   (2h 30m 40s)
verdict     QUALIFICATION FAILED: 2 stage(s) failed
root cause  ONE defect, in a test helper. The second failed stage is entirely
            downstream of the first
```

**THE LAUNCHER REPORTED EXIT CODE 0.** The background task that ran
`run-qualification.cmd` was recorded as "completed (exit code 0)" while the
harness itself wrote `QUALIFICATION FAILED: 2 stage(s) failed` and
`qualify.cmd exit 2`. `FREEZE.md` says the verdict is the harness's own
sentence plus the failed-stage count and **not** the launcher's exit code; this
run is why that line is in `FREEZE.md`, and it is the fourth time this session
that a batch launcher's exit code has disagreed with what the run did.

## The failed stages

```text
release-ext build          exit 1   09:39:15   (15m 47s in; 66m 28s into the run)
release-ext ctest          skipped: the build failed
repeat release-ext         exit 8   10:29:49
```

Everything else passed, and `qualify.cmd` correctly carried on to the remaining
preset rather than abandoning the run, so the two Debug presets produced
complete evidence despite the failure.

## Root cause

```text
tests/cfd/CfdMomentumTests.cpp:629    out.pop_back()  in shortenedWithLeadingNan
```

```text
In function 'constexpr void std::destroy_at(_Tp*) [with _Tp = double]',
  inlined from 'std::allocator_traits<...>::destroy' at alloc_traits.h:738:19,
  inlined from 'std::vector<double>::pop_back()' at stl_vector.h:1451:24,
  inlined from 'shortenedWithLeadingNan' at CfdMomentumTests.cpp:629:17,
  inlined from 'CATCH2_INTERNAL_TEST_130()' at CfdMomentumTests.cpp:2236:69:
stl_construct.h:88:9: error: array subscript -1 is outside array bounds of
       'double [1152921504606846975]' [-Werror=array-bounds=]
cc1plus.exe: all warnings being treated as errors
```

`pop_back()` destroys `*(end() - 1)`. At `-O3`, with the helper inlined into its
caller, GCC cannot prove the vector non-empty and bounds that subscript at -1.
The project builds Release with `-Werror`, so the file compiles in `debug-ext`
and breaks the build in `release-ext`.

**EXACTLY ONE DIAGNOSTIC WAS EMITTED FOR THAT TRANSLATION UNIT**, which is
load-bearing: `cc1plus` analyses the whole unit and reports every warning before
`-Werror` ends it. The file's four other `pop_back()` calls (lines 1939, 1948,
1957, 2228) and its six `front()` calls (2043-2068) were therefore analysed and
are clean. The fix is one line, not a sweep.

## Why the pre-freeze Release build did not catch it

`FREEZE.md` recorded `release-ext builds clean under -Werror after the
array-bounds fix` under *Pre-freeze state of the gates*. **That claim was
measured on an earlier tree and was not true of the frozen tree.**

The first instance of this defect was in `withPoisonAt`, found before the freeze
by building `release-ext`, and fixed. `shortenedWithLeadingNan` sits thirty
lines below it, carries the same defect, and was written **after** that build —
during the round-3 mutation work that closed M19. No `release-ext` build was run
after that edit, so the second instance was never compiled at `-O3` until the
gate compiled it.

Two things were wrong, and both are mine:

```text
1  A FIX AT ONE SITE WAS TREATED AS A FIX OF A CLASS. withPoisonAt's comment
   claimed the class had been caught "before the freeze rather than inside
   it". It had not. The comment has been corrected rather than deleted,
   because the overclaim is the finding
2  A PRESET BUILT BEFORE THE LAST EDIT WAS RECORDED AS A PRESET THAT BUILDS.
   The freeze's own hashes prove the tree changed after that build; nothing
   checked the build against them
```

The cost is this 2h30m run plus its replacement. `P19-DIV-001` lost three hours
to the same shape of mistake and `FREEZE.md` cites it; the lesson recorded there
was "build the second preset before freezing", and the lesson this run adds is
**build it after the last edit to anything it compiles.**

## What this run nevertheless establishes

The three frozen figures were counted off the tree rather than predicted, and
the stages that ran confirm all three **exactly**:

```text
debug-ext         100% tests passed out of 6601        matches 6601
debug-shared-ext  100% tests passed out of 6601        matches 6601
repeat debug-ext  100% tests passed out of 3306        matches 3306
                  16530 Start markers / 3306 result lines = 5.000 exactly
```

So `FREEZE.md`'s counting method and its three figures are validated, including
the marker-to-result-line distinction that `P19-CONV-001`'s freeze got wrong.
The fix changes one helper's body and adds no test case, so attempt 2 must
reproduce 6601, 3306 and 16530 unchanged — a prediction, recorded here before
the re-run, that attempt 2's log can be checked against.

The tree identity held across the whole run:

```text
qualified source trees before the first build  ==  after the last test run
all eight subtree hashes identical, HEAD db53c5095cd091a9abdb7cf143fd02326ef9a2b8
```

Nothing moved under the frozen surface during the run. The qualification is void
because the tree has changed **since** it, by the fix above, which is the
correct reason for a void qualification rather than a mid-run edit.

## The downstream stage

`repeat release-ext` selected 352 tests where the frozen figure is 3306, and 147
of them failed:

```text
58% tests passed, 147 tests failed out of 352
352  = 205 architecture + 147 cli/gui                     EXACTLY
2954 = 3306 - 352, the Catch2 tests the filter matches     DERIVED
3306 = 2954 + 205 + 147                                   EXACTLY
1025 = 205 x 5 "Passed" lines, so each architecture test
       did run its five executions                        EXACTLY
```

Catch2 test discovery runs the test binary at **build** time. The build failed,
so `bettercad_tests.exe` and the CLI and GUI binaries were absent: the 2954
Catch2 tests the repeat filter matches were never registered, the 147 cli/gui
tests failed for a missing executable, and the 205 architecture tests passed
because they are CMake scripts that need no binary. Every figure above
decomposes exactly, so this stage carries no independent defect.

## Disposition

```text
RESULT      FAIL
ACTION      stop -> diagnose -> fix -> re-run from clean, per CLAUDE.md
FIX         tests/cfd/CfdMomentumTests.cpp, shortenedWithLeadingNan built one
            short from an explicit range instead of copied and popped; two
            comments corrected, one of which was an overclaim this run exposed
RE-RUN      attempt 2, from a new freeze, after release-ext is built TO
            COMPLETION on the fixed tree -- attempt 1 stopped at target 709 of
            813, so roughly a hundred targets and the link were never compiled
            and are not yet known to build
```

No gate was relaxed and no tolerance moved.
