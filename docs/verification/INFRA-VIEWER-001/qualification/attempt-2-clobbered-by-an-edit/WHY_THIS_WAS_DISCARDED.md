# Attempt 2 — passed, and discarded anyway

```text
RAN:      16:45:29 -> 20:38:54
VERDICT:  "Qualification passed: every stage exited 0."  (run-qualification.out)
DISCARDED BECAUSE: its consolidated stage record was destroyed before it could
          be read, by an edit I made to the running batch file.
```

## What happened

`cmd.exe` reads a batch file **incrementally, by byte offset**, while it
executes it. At 16:47 — two minutes into a run that would last four hours — I
edited `run-qualification.cmd` to soften an overstated comment about the
out-of-memory failure. The edit changed the file's length. When `qualify.cmd`
returned, `cmd.exe` resumed reading `run-qualification.cmd` at an offset that
no longer meant what it had meant, and executed `call qualify.cmd` a second
time.

That second pass began at 20:38:56, two seconds after the first finished, and
immediately truncated `qualification-times.txt` and overwrote `debug-ext`'s
configure, clean and build logs before it was stopped.

## What survived, and why it is not enough

```text
INTACT    rebuild-debug-ext.log      17:11:43   (no work to do)
INTACT    ctest-debug-ext.log        17:28:11   721 KB, 3151/3151
INTACT    every release-ext log
INTACT    every debug-shared-ext log
INTACT    both ctest-repeat logs     191/191 each
INTACT    run-qualification.out      "every stage exited 0"
LOST      qualification-times.txt    truncated -- the stage-by-stage record
LOST      configure/clean/build-debug-ext.log
```

The verdict line is `qualify.cmd`'s own, printed only when no stage failed, and
the surviving `rebuild-debug-ext.log` — a no-op rebuild immediately after the
build — is good indirect proof that `debug-ext` built completely.

**It is still not enough.** The rule is that every claim has a log behind it,
and "the debug build passed" would be resting on a transcript and an inference
rather than on this directory. A qualification that has to be argued for is not
a qualification. So this one is kept here as the record of a real pass, and the
milestone is qualified by a single uninterrupted run instead.

## The lesson, which is cheap to state and was not cheap to learn

**Never edit a `.cmd` file while `cmd.exe` is running it.** Not a comment, not
whitespace. The tree being frozen is not the only thing that must hold still
during a qualification; so must the script driving it.

## What is kept here, and what was not committed

Kept: the truncated `qualification-times.txt`, the verdict in
`run-qualification.out`, and this attempt's configure, clean, build and
rebuild logs — everything needed to see that it ran, that it built 592
objects per preset with no warnings, and that it reported a pass.

**Not committed: this attempt's five CTest transcripts** (2.4 MB). They showed
`3151/3151` three times and `191/191` twice, which is exactly what the
qualifying run's own transcripts show completely and from a single
uninterrupted pass. Keeping a second copy would have doubled this milestone's
evidence directory against every previously qualified milestone without adding
anything checkable. Said here rather than left to be noticed.
