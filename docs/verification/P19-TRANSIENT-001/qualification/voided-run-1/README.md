# The first qualification run, VOIDED — and why it is not a product failure

```text
LAUNCHED:  2026-11-10 06:15:46
DIED:      2026-11-10 07:18:50   (63 minutes in, of a ~3.5 hour run)
VERDICT:   QUALIFICATION FAILED: 5 stage(s) failed
CAUSE:     the environment running the harness was torn down mid-run
PRODUCT:   not implicated -- see the diagnosis below
ACTION:    re-run from clean. This run proves nothing either way.
```

## What the harness said

Its own two statements, which are the verdict and which are not this
directory's to soften:

```text
run-qualification.out     QUALIFICATION FAILED: 5 stage(s) failed.
qualification-times.txt   qualification finished ... 5 stage(s) failed
```

**A FAILED RUN IS A FAILED RUN AND THIS ONE IS KEPT AS ONE.** Nothing below
converts it into a pass. What the diagnosis establishes is narrower and is worth
establishing exactly: the failure is attributable to the environment and not to
the tree, so the correct response is to RE-RUN from clean rather than to change
any code.

## The stage timeline

```text
debug-ext  configure       exit 0           06:15:53
debug-ext  clean           exit 0           06:15:54
debug-ext  build           exit 0           06:34:46   18m52s, 0 error 0 warning
debug-ext  no-op rebuild   exit 0           06:34:46
debug-ext  ctest           exit 8           07:18:49   <- ran 44 minutes
release-ext configure      exit -1073741502 07:18:49
release-ext build          SKIPPED: the configure failed
release-ext ctest          SKIPPED: the configure failed
debug-shared-ext configure exit -1073741502 07:18:49
debug-shared-ext build     SKIPPED
debug-shared-ext ctest     SKIPPED
repeat release-ext         FAILED: the filter matched no tests
repeat debug-ext           FAILED: the filter matched no tests
```

`-1073741502` is `0xC0000142`, `STATUS_DLL_INIT_FAILED`: a process that could
not start. Both configures returned it **within 30 milliseconds** of being
asked, which is not a configure failing — it is a configure that never ran. The
two repeat stages then matched no tests because no build tree had been
configured for them.

So four of the five failed stages are one event: the environment stopped being
able to launch processes at 07:18:49.

## The fifth stage, and why it is the same event

`debug-ext ctest exit 8` is the one that needs real work, because it ran to
completion and a non-zero ctest exit is exactly what a product failure looks
like.

```text
95% tests passed, 375 tests failed out of 6965
```

Three hundred and seventy-five failures is not a defect; it is a pattern. The
reasons, counted off the failure list:

```text
374   (Exit code 0xc0000142)      the same process-launch failure
  1   (Failed)                    compile_fail.thermalload
                                    .a-prepared-load-constructed-directly
```

The 374 are the teardown arriving in the middle of the compile-fail groups,
where every case is a real compiler invocation and therefore a process launch.
The affected test numbers span 5890 to 6937 — the compile-fail range — and the
earlier 5889 tests, which include every unit test in the tree, had already
passed.

### The one that was not 0xc0000142, run again in isolation

```text
in the voided run    ***Failed    3.39 sec
run again now         Passed     11.89 sec
```

**The timing is the diagnosis.** A compile-fail case passes by compiling,
failing, and emitting a diagnostic its regex matches. This one takes 11.89
seconds to do that honestly. In the voided run it finished in 3.39 seconds —
less than a third — because the compiler subprocess was killed part way, so the
case still exited non-zero (which the gate expects) while emitting something the
regex could not match. That is the same teardown as the other 374, presenting
differently because this case got far enough to be judged.

It is an INHERITED thermal gate, untouched by this milestone, and it passed in
`P19-STEADY-001`'s qualification one day earlier.

### None of the new tests is in the failure list

Checked directly: no `unit.CfdTimeGrid_`, no `unit.CfdTransient_`, no
`compile_fail.cfdtransient` case appears among the 375. The 5889 tests that ran
before the teardown include all 34 of the new suite's cases.

## What this run DID establish, and it is not nothing

The stages that completed before the teardown completed cleanly, and their logs
are kept here rather than discarded:

```text
the eight frozen identities    read by the harness BEFORE its first build, and
                               identical to FREEZE.md's eight
debug-ext configure            exit 0
debug-ext clean                exit 0
debug-ext build                exit 0, and build-debug-ext.log holds
                               0 `error:` and 0 `warning:` under -Werror
debug-ext no-op rebuild        exit 0
the first 5889 tests           passed, the new suite among them
```

That is a real datum about the tree: it builds clean from scratch under
`-Werror` in one preset and its tests pass. It is **not** a qualification, which
needs three presets, two repeat stages, and a post-run identity reading — and
the post-run reading is literally empty in this run's times file
(`qualified source trees after the last test run:` with nothing after it),
because the harness could not launch git either.

## Why the freeze survives

CLAUDE.md voids a qualification when *"source or tests change after the
freeze"*. Nothing changed: the diagnosis above required running one existing
test and moving ten log files into this directory, and `docs/` is outside the
frozen surface. The eight identities were re-read before the second run was
launched and are unchanged, so the second run qualifies the same tree this
directory's `FREEZE.md` froze.

## What was changed about HOW the run is launched

Nothing in the harness. The second run uses the same
`run-qualification.cmd` — byte-identical, since it is the one in the directory
above — launched from a terminal that outlives a single turn, so that a
three-and-a-half-hour run is not racing a session boundary. The first run was
launched as a backgrounded child of the agent's own shell, which is what made it
mortal.

## What is committed here, and what is not

The bulk `ctest-debug-ext.log` is **not** committed. It is 1.57 MB of a run that
proves nothing, and `P19-STEADY-001` set the practice for a voided attempt: keep
the harness's verdict statements, the stage timeline, the diagnosis, and a small
extract of the decisive lines. `CLAUDE.md` asks for a reason before a large
generated file is committed, and "a log nothing cites in full" is not one.

```text
kept in full     README.md, qualification-times.txt, run-qualification.out,
                 run-qualification.err, the three configure logs, and the
                 clean / build / no-op-rebuild logs -- the build log is the one
                 that carries the 0 error / 0 warning datum this file cites
extracted        ctest-debug-ext-extract.txt, 398 lines: ctest's own summary,
                 every distinct failure reason COUNTED, the proof that no
                 unit.CfdTransient_, unit.CfdTimeGrid_ or
                 compile_fail.cfdtransient case is in the failure list, and the
                 complete 375-line failure list as ctest printed it
not committed    ctest-debug-ext.log itself
```

Every figure this file states about that log is in the extract, counted rather
than summarised, so the claim and its support are committed together.
