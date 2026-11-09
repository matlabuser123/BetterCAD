# P19-LINSOLVE-001 mutation probes — three rounds and what each one changed

Driver: [probe.py](probe.py), probe sets [probes.json](probes.json),
[probes-round2.json](probes-round2.json),
[probes-round3.json](probes-round3.json). Each probe replaces ONE piece of text
in one production file, rebuilds `debug-ext`, runs `[linalg]` and `[linsolve]`,
reverts, and re-hashes all four watched files. The run stops dead if a revert
does not restore a file exactly.

```text
KILLED    a suite failed -- what a behavioural probe must do
COMPILER  rejected at compile time -- STRONGER than a test kill
SURVIVED  every suite passed; correct ONLY for the no-op control
```

**THE PROBE SET IS NOT THE AUTHOR'S.** Twenty of the twenty-one in round one are
the test-adequacy review's own predictions: it was asked for one-line edits it
believed no test would catch, and it produced twenty with a prediction for each.
Rounds two and three add probes against code the review never saw — the fixes
themselves.

## Round one — [mutations-round1.out](mutations-round1.out)

```text
21 attempted      19 KILLED, 2 SURVIVED, then 1 ERROR stopped the run
```

Of the twenty mutations the review predicted would survive, nineteen were caught
after its findings had been fixed. **Both survivors were in the fixes rather than
in the original code**, which is the argument for probing a repair instead of
trusting it.

**M4c — the wrong half of a two-part guard was carrying the tests.** Removing
only the monotonicity half of the new row-offset guard survived: all five
malformed-offset inputs were caught by a DIFFERENT clause — two by the interior
bound, three by the pre-existing front and back checks — so the decrease itself
was never exercised. Fixed with `{0, 2, 1, 2}` over three rows, in which the
first offset is 0, the last is the entry count, every offset is in bounds, and
only the `2 -> 1` step is wrong.

**M9a — a layer's guard that the layer below made invisible.** Narrowing CFD's
predicate from `!isfinite` to `isnan` survived, because `linalg` repeats the
check and reports the same problem with the same index, which `translate` maps
to the same `ValueNotFinite`. Identical verdict, nothing for an assertion to
see. Only CFD's check ORDER distinguishes the layers, so the infinity was moved
onto an ASYMMETRIC matrix: CFD checks the right-hand side before symmetry, and
without its guard the symmetry check answers first.

**M19 — ERROR, and the author's own fix caused it.** The probe text
`failure.reportedRank = out.solution.reportedRank;` occurred twice after the
non-finite-diagonal branch gained the same line. The driver refuses to
substitute an ambiguous match and stopped, which is why rounds two and three
exist rather than a silently half-applied table.

## Round two — [mutations-round2.out](mutations-round2.out)

```text
8 probes          6 KILLED, 1 SURVIVED, NO-OP CONTROL SURVIVED
```

M4c, M9a and M19 all killed, and three new probes against the R-diagonal scan —
including the dangerous one, initialising the diagonal to `1.0` so a column
Eigen discarded reads as healthy — all killed.

**M4e — AND THE SURVIVOR WAS A PROOF, NOT A MISSING TEST.** Removing the
interior-bound half of the guard survived, and no test could have caught it:

```text
the dimension clause already requires   rowStart.back() == inner.size()
monotonicity requires                   rowStart[i] <= rowStart[i+1]
therefore, by transitivity               rowStart[i] <= inner.size()  for every i
```

So an interior offset can exceed the stored entries ONLY if the offsets also
decrease somewhere. The bounding clause could not fire for any input, which is
precisely what this project's own rule rejects — `CfdBoundary.hpp`: "An
enumerator no input can produce is worse than no enumerator" — and the rule was
being cited elsewhere in this same milestone to justify omitting other checks.

**The clause was removed from both kernels**, monotonicity carries the whole
weight, and the original reproducer is still refused by both. The guard is
simpler than the one the audit's finding first produced, and provably
sufficient rather than defensively redundant.

## Round three — [mutations-round3.out](mutations-round3.out)

Dropping the redundant clause MOVED where memory safety comes from, so the two
clauses the proof now rests on were both probed, in both kernels.

```text
M4a  the monotonicity guard removed, general kernel          KILLED
M4b  the same, SYMMETRIC kernel                              KILLED
M4d  the back-equality clause the proof leans on             KILLED
M4f  monotonicity made STRICT, refusing a legitimate empty row  KILLED
M18  NO-OP CONTROL                                           SURVIVED
```

`M4d` had already been killed in round one, but that was with the redundant
bound still present as a backstop; it had to kill on its own merits or the proof
would have had only one leg. `M4f` probes the opposite error from the original
defect: `>` becoming `>=` refuses any empty row, which is legitimate, and the
all-zero-matrix case catches it.

## Totals

```text
behavioural probes, all rounds   29 attempted, 29 killed after the fixes
survivors across the campaign     3, each a real weakness, each resolved:
                                    M4c  a test that exercised the wrong clause
                                    M9a  a guard the layer below made invisible
                                    M4e  a clause that could not fire, removed
probe-definition errors           1 (M19), caused by the author's own fix, and
                                    caught by the driver rather than papered over
no-op control                     SURVIVED in all three rounds
reverts verified by hash          every probe, all three rounds, all four files
```

**THE NO-OP CONTROL IS WHY THIS TABLE CAN BE READ AT ALL.** On `P19-PVCOUPLE-001`
the driver was launched twice through `cmd /c` from Git Bash, had its `/c`
path-converted into a drive root, started an interactive shell, wrote nothing and
EXITED 0 both times. A probe table from a driver that never ran reads exactly
like strong evidence. This driver is Python, launched through PowerShell, and the
control's survival together with the matching hashes is what says the table is a
measurement.
