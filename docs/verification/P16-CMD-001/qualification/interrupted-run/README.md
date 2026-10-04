# The first qualification attempt, stopped at two hours

```text
STARTED:  2026-10-04 22:38:55
STOPPED:  2026-10-05 00:38:55, by the harness that launched it, not by a failure
TREE:     b9593bbc9276e6dbd809a93945152386837c0061  (the frozen tree)
```

This run was not a failure and it is not the milestone's qualification. It is
kept because deleting a two-hour run that passed five of its stages, and
replacing it with a second run, is exactly the sort of gap that makes evidence
hard to trust later.

## What it completed

```text
debug-ext         configure 0  clean 0  build 0  no-op rebuild 0
                  ctest 3273 / 3273, 100%, 970.59 s
release-ext       configure 0  clean 0  build 0  no-op rebuild 0
                  ctest 3273 / 3273, 100%, 977 s
debug-shared-ext  configure 0  clean 0  build 0  no-op rebuild 0
                  ctest INTERRUPTED -- no result
repeat stages     never reached
```

## Why it stopped

It was launched through a wrapper with a two-hour ceiling, and the
qualification needs about two hours and ten minutes. The wrapper was killed at
its limit and took `cmd.exe` with it — the clock shows the kill at exactly two
hours after the start, and no build or test process survived it.

Nothing in the tree or the logs indicates a defect: every stage that ran
exited 0, and the two completed presets each passed all 3273 tests.

## What was done instead

The source tree was re-fingerprinted first and still matched the freeze
exactly, `b9593bbc`, component for component — so a second run qualifies the
same tree rather than a drifted one.

The qualification was then re-run **in full, once, detached**, so that no
wrapper lifetime could bound it. The whole run was repeated rather than only
its missing stages: `qualify.cmd`'s exit code is the number of failed stages
across every preset, and that single number over one coherent set of logs is
the artifact the gate asks for. Splicing two partial runs together would mean
reasoning about which log belonged to which invocation, which is the ambiguity
that voided INFRA-VIEWER-001's first qualification.

The results here are corroboration for the final run, not a substitute for it.
