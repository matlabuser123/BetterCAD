# P19-PVCOUPLE-001 mutation probes — the two rounds and what they changed

Driver: `probe.cmd` + `subst.py` + `probes.json`. Each probe replaces ONE piece
of text in `src/cfd/CfdCoupling.cpp`, rebuilds `debug-ext`, runs the `[pvc]`
suite, reverts, and re-hashes both files. The run stops dead if a revert does
not restore a file exactly.

```text
KILLED    the suite failed -- what a behavioural probe must do
COMPILER  rejected at compile time -- STRONGER than a test kill
SURVIVED  the suite passed; correct ONLY for a documented-unreachable state
          and for the no-op control
```

**THE PROBES WERE WRITTEN FROM THE TEST-ADEQUACY REVIEW'S OWN PREDICTIONS.**
That review listed 22 mutations it expected to SURVIVE and named, for each, why
the suite could not see it. Round one applied them after the review's findings
had been fixed, to check whether the fixes actually bit.

## Round one — [mutations-round1.out](mutations-round1.out)

```text
22 behavioural probes      19 KILLED, 1 by the COMPILER, 3 SURVIVED
1 unreachable-state probe  SURVIVED, which is the correct answer
NO-OP CONTROL              SURVIVED
both files re-hashed       MATCH pristine before and after every probe
```

So of the 22 the review predicted would survive, 19 were now caught — and three
still were not. Each of the three pointed at a different kind of weakness, and
all three are recorded here rather than quietly re-probed.

### M3 — a premise satisfied by floating-point noise

Reversing `interpolate` — the exact `1 - w` defect `ADR-075` Q1 was written
about — SURVIVED. The value test added for exactly this purpose could not see
it, because on the uniform chain mesh every internal face's weight is one half
to within a bit or two, and **at `w = 0.5` the two forms agree exactly**.

The test's premise check asked whether any weight was "not exactly 0.5", which
floating-point noise satisfies. That is the vacuous-premise pattern the same
review had just taught, reproduced in the test written to answer it. Fixed with
a GRADED chain whose cells differ in size by a large factor, and a premise
asserted WITH A MARGIN: the largest `|weight - 0.5|` must exceed `0.05`.

### M9 — a flag that could not be false, so it was deleted

Replacing `summary.continuityClosed` with a literal `true` SURVIVED, and no test
could have killed it. The bound was `systemResidual + floor` and the quantity
compared against it was `residuals.continuity` — and `ADR-075` Decision 5's
identity says those two ARE the same number. The comparison held by
construction, for every possible input.

`CfdBoundary.hpp` states this project's rule for the same shape of problem: "An
enumerator no input can produce is worse than no enumerator." An unfalsifiable
flag is that rule's other face, so **`continuityClosed` and `closureBound` were
deleted** rather than documented as always true. What they claimed to report is
reported by `systemResidual`, a measured number, and the identity is asserted
directly by two tests. A test that had asserted the bound SHOULD move with the
solver's report was replaced by its opposite: nothing this operator measures may
depend on what a port chooses to report about a solution it has already
returned.

### M16 — a factor of two that changed no verdict

Dropping the `0.5` from the compatibility throughput SURVIVED, because every
case in the suite sat either at a relative residual of exactly `2.0` or at
exactly `0.0`, and halving a residual moves neither across a `1e-13` bound.
Fixed with two marginal cases — a tilt of `1.5e-13`, which must be refused, and
one of `2.0e-14`, which must not — so the factor is load-bearing.

## Round two — [mutations.out](mutations.out)

```text
M3b  the interpolation reversed, on the GRADED mesh          KILLED
M9b  the system residual discarded (replaces the flag)       KILLED
M16b the compatibility throughput loses its half, marginal
     cases present                                           KILLED
M9c  the pinned row NOT excluded from the system residual     COMPILER
NO-OP CONTROL                                                 SURVIVED
both files re-hashed                                          MATCH pristine
```

## Two things a reader will notice in the two logs

**The baseline assertion count FELL between the rounds**, from `5092
assertions in 79 test cases` to `4950 assertions in 79 test cases`, with the
case count unchanged. That is the three repairs: `continuityClosed` and
`closureBound` were deleted and the assertions over them went with them, the
interpolation test was rewritten onto a graded mesh, and the compatibility
test gained two marginal cases. 4,950 in 79 is the figure the three qualified
binaries report, so round two's baseline is the qualified one. The exact
per-test split is not recoverable from these logs and is not claimed here.

**`probes.json` holds round TWO only.** It was overwritten rather than
versioned, so round one's twenty-four exact substitutions are not reproducible
from the committed artifacts; what survives of round one is each probe's
one-line description and verdict in `mutations-round1.out`, with the file
hashes that bound it. Recorded as a gap rather than papered over: the round-one
table can be read but not re-run.

## Totals

```text
behavioural probes caught   23 of 23 after the fixes
                            (19 + 1 compiler in round one, 3 + 1 compiler in
                             round two)
round one survivors          3, each a real weakness, each fixed and re-probed
unreachable state survived   1 (M23: the disconnected-region flag, which no
                             fixture builds -- recorded as unreachable rather
                             than claimed as covered)
no-op control                SURVIVED in both rounds, as a control must
reverts verified by hash     every probe, both rounds
```

**THE NO-OP CONTROL IS WHY THIS TABLE CAN BE READ AT ALL**, and on this
milestone it earned its place twice over. Before either round produced a result,
the driver was launched two ways from the session's Bash tool and both times
`cmd.exe` had its `/c` path-converted into `C:\`, so it started an interactive
shell, wrote nothing, and **exited 0**. A probe table from a driver that never
ran would have looked like strong evidence. It was launched through PowerShell
instead, and the control's survival plus the pristine hashes are what say the
table is a measurement.
