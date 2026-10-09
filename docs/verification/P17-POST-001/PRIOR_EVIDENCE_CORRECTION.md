# A correction to two earlier milestones' qualification records

```text
FOUND BY:     P17-POST-001, while copying the harness for its own run
FIXED:        2026-10-10, on an explicit instruction
AFFECTS:      documentation only. No qualification result moves.
```

## What was wrong

```text
docs/verification/P17-SOLVE-001/qualification/run-qualification.cmd
    two bare `OLD_END` lines, at 69 and 95
docs/verification/P17-ASSEMBLY-001/qualification/run-qualification.cmd
    one bare `OLD_END` line, at 85
```

They are heredoc terminators, leaked by the sessions that wrote those files —
a shell `<<'OLD'` block whose terminator ended up in the output instead of
closing it. Each sat in the middle of a `rem` comment block, between two
complete comment lines, so **no content was lost**: the sentences on either
side are whole and the list entries they separate are intact.

`cmd.exe` reads a batch file line by line and tried to execute them:

```text
'OLD_END' is not recognized as an internal or external command,
operable program or batch file.
```

which is the first thing in each file's `run-qualification.err`.

## Why no result moves

```text
the runs were not affected        an unrecognized command prints to stderr and
                                  cmd.exe carries on to the next line. Both
                                  files' executable blocks -- setlocal, the
                                  five `set` lines, the `call`, the echo and
                                  the endlocal -- are below the comment block
                                  and were reached and run normally
every stage still exited 0        recorded in each milestone's
                                  qualification-times.txt and stage table
the test counts are unchanged     3615/3615 x 3 for P17-ASSEMBLY-001,
                                  3647/3647 x 3 for P17-SOLVE-001
the source fingerprint is
  unchanged                       these files are documentation and sit
                                  outside the eight fingerprinted paths
                                  (apps include src tests examples cmake
                                  CMakeLists.txt CMakePresets.json), so the
                                  "qualified tree == committed tree" proof in
                                  each FREEZE.md is untouched
```

So this is a defect in a **record**, not in a result: a file that calls itself
"the exact invocation" contained three lines that were not part of it, and
re-running it would have produced three spurious errors.

## What was changed

```text
removed      the three bare OLD_END lines, and nothing else
added        a rem note at the top of each file recording the correction, the
             date, the milestone that made it, and where the record of the
             original behaviour still is
NOT touched  run-qualification.err in either directory
NOT touched  qualify.cmd in either directory -- still
             d313a64070718c44fae290ac042fe259d1a03c8b in all three, including
             P17-POST-001's
NOT touched  every log, the stage tables, the times files, the READMEs, the
             FREEZE files
```

Verified by diff: **3 deletions, 40 insertions, 2 files**, and the only
deleted lines are the three `OLD_END`s.

### The `.err` files are deliberately left alone

They are the record of what the runs actually produced. Editing them to match
the corrected `.cmd` would make the evidence tidier and less true, which is
the one thing evidence may not be. So each corrected file says, in its own
note, that the `.err` beside it still records the original behaviour and why.

## Verification after the change

```text
check                                             P17-SOLVE  P17-ASSEMBLY
-----------------------------------------------------------------------------
bare OLD_END lines remaining                          0            0
lines not starting with rem/set/call/echo/
  setlocal/endlocal/@echo                             0            0
executable block intact and unchanged               yes          yes
  (5 set, 1 call, 1 echo, setlocal, endlocal)
QUALIFY_REPEAT filter unchanged                      yes          yes
qualify.cmd hash unchanged                           yes          yes
run-qualification.err unmodified                     yes          yes
```

Neither file was executed as part of this correction. Running one would start
a fresh three-hour qualification and overwrite the logs it is evidence for —
which is the opposite of the point.

## How it was caught, and what stops it recurring

It was caught by reading P17-SOLVE-001's invocation in order to copy the
harness for P17-POST-001's own run — the sort of thing only reuse finds. The
`.err` files had recorded it faithfully for two milestones and nobody had read
them.

P17-POST-001's own copy was written in one pass and then checked:

```bash
grep -nvE "^(rem|set |call |echo |setlocal|endlocal|@echo)" run-qualification.cmd
# must print nothing
```

That one-line check is now part of this milestone's freeze procedure, and its
result is recorded in [FREEZE.md](FREEZE.md).
