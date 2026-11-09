# P19-STEADY-001 mutation probes — three rounds and what each one changed

Driver: [probe.py](probe.py), carried from `P19-LINSOLVE-001` with its watched
files and filters changed. Probe sets: [probes.json](probes.json),
[probes-round2.json](probes-round2.json),
[probes-round3.json](probes-round3.json). Each probe replaces ONE piece of text
in one production file, rebuilds `debug-ext`, runs `[cfd][steady]` and
`[cfd][data]`, reverts, and re-hashes all four watched files. The run stops dead
if a revert does not restore a file exactly.

```text
KILLED    a suite failed -- what a behavioural probe must do
COMPILER  rejected at compile time -- STRONGER than a test kill
SURVIVED  every suite passed; correct ONLY for a no-op control or a
          documented-unreachable case
```

**THE PROBE SET IS NOT THE AUTHOR'S.** Twenty-two of the twenty-three are a
test-adequacy review's own predictions: it was asked for one-line edits it
believed no test would catch, and it produced twenty-two with a prediction for
each. The twenty-third is the DEFECT the *correctness* review found, added so
that its fix is pinned rather than trusted.

One of the review's twenty-two was dropped before the campaign: `M11` targeted
four `validate` calls that had already been removed as unreachable — which the
same prediction is what revealed, so it did its job without being run.

## Round one — [mutations-round1.out](mutations-round1.out)

```text
22 probes        8 KILLED, 14 SURVIVED, both no-op controls SURVIVED
```

**Three of the review's SURVIVE predictions were already dead, and the other
review is why.** `M5` (hardcode `massBalanceDefined` true), `M7` (swap `Upwind`
for `Central`) and `M8` (drop the pressure gradient) were all KILLED — by the
closed-domain test and the gravity regression test, both of which were written in
response to the *correctness* review. Neither reviewer saw the other's output, and
their findings closed each other's gaps.

**Twelve real gaps, and one of them mattered much more than the rest.**

`M12` replaced the published mass flux with the PRE-correction flux — the
predicted `phi*` the momentum solve produces before the pressure correction — and
survived the whole suite. Publishing that would publish a NON-CONSERVATIVE field,
which is the one thing SIMPLE's flux correction exists to prevent, inside a result
that claims to be a solution. Nothing in the suite looked at conservation at all.

The fix is a check through `divergeCfdMassFlux`, `P19-DIV-001`'s qualified
operator, which knows nothing about this milestone:

```text
published flux   worst per-cell |sum phi| / throughput   7.07e-17
                 global imbalance                        7.32e-17
the SAME measure on the pre-correction flux              7.26e-05
```

Twelve orders of magnitude, so the bound is set by the defect's signature rather
than fitted to the passing number. It is also the independent validation of the
SOLUTION that the evidence otherwise lacked: the dense-reference comparison
validates the SOLVE as interchangeable, and a wrong loop would be wrong
identically in both runs, whereas conservation is a property of the published
field alone.

## Round two — [mutations-round2.out](mutations-round2.out)

```text
13 probes        8 KILLED, 5 SURVIVED, no-op control SURVIVED
```

Ten closures re-probed, plus the two expected to survive and a control. Nine of
the ten were dead. **The tenth, `M2`, survived again, and the author's fix was
the reason.**

`M2` makes `report.outerIterations` off by one. The first attempt to close it
asserted `outerIterations <= maxIterations`, which is worthless when the budget
is 400 and the run converges in sixty-one iterations: `completed + 1` is still
far under the budget. That was a prediction, and the probe said it was wrong.

## Round three — [mutations-round3.out](mutations-round3.out)

```text
2 probes         M2 KILLED, no-op control SURVIVED
```

What closes `M2` is an **independent counter**, not a tighter bound on the
orchestrator's own number. Every outer iteration performs a fixed number of
linear solves — three momentum components in the predictor and one pressure
correction — so a counting solve port counts the iterations without consulting
the report at all. Measured, then pinned:

```text
solve-port calls 244 over 61 reported iterations, ratio exactly 4
```

## Totals

```text
behavioural probes          20 distinct, excluding the two no-op controls
killed after the fixes      17
surviving BY DESIGN           3, each a documented limitation with a reason:
                              M9  the gradient method: both converge, and
                                  distinguishing them needs an analytic
                                  reference this subsystem lacks       (L17)
                              M10 the four mesh-stage refusals: no fixture can
                                  pass the model gate and then fail one  (L16)
                              M18 the face-count half of a guard: needs two
                                  layouts with equal cell counts         (L15)
no-op controls              M15 and M21, SURVIVED in every round they ran
reverts verified by hash    every probe, all three rounds, all four files
author predictions wrong      2, both recorded above rather than quietly fixed
```

**THE NO-OP CONTROLS ARE WHY THIS TABLE CAN BE READ AT ALL.** On
`P19-PVCOUPLE-001` the driver was launched twice through `cmd /c` from Git Bash,
had its `/c` path-converted into a drive root, started an interactive shell,
wrote nothing and EXITED 0 both times. A probe table from a driver that never ran
reads exactly like strong evidence. This driver is Python, launched directly, and
the controls' survival together with the matching hashes is what says the table
is a measurement.

**AND THE SCHEME PROBE IS PINNED BY ACCIDENT.** `M7` was killed by the gravity
test's quantitative `dp/dy` check, which is a side effect of that test rather
than its purpose. Of the two hard-coded numerical choices, one is caught
incidentally and the other (`M9`) is not caught at all. `ADR-077` L12 and L17
record that, and L12 reports the underlying scope decision to the owner:
`ADR-073` Decision 5 says both choices belong in the analysis definition, which
is where they would become observable.
