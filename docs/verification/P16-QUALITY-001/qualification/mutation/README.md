# P16-QUALITY-001 — mutation testing harness

```text
PURPOSE:  break the implementation on purpose, and see whether the suite
          notices. A mutation that SURVIVES marks a gap in the tests.
```

Each entry in `mutations.json` is one plausible implementation mistake: a
label, the exact text to find in `src/meshing/MeshQuality.cpp`, and the text to
put in its place. `mutate.sh` applies them one at a time, rebuilds, runs the
`[quality]` suite, records the verdict, and restores the pristine source at the
end — including after a failure, so a mutation cannot be left in the tree.

```text
mutate.sh          the loop: apply, rebuild, run, record, restore
apply.py           applies one mutation by index, or restores the snapshot.
                   Refuses a pattern that is absent or that occurs more than
                   once, so a mutation can never land somewhere unintended.
mutations.json     the 24 mutations
results-final.txt  the run against the COMMITTED implementation
```

An earlier run, against the tree before the adversarial review's three fixes,
is what produced finding 3 -- one mutation survived it. **That run's raw log
was overwritten by this one**, because the harness writes to a fixed path;
stated here rather than reconstructed, since a log written out by hand is not a
log. What survives of it is the finding, recorded in
[ADVERSARIAL_REVIEW.md](../../ADVERSARIAL_REVIEW.md), and the mutation itself,
which is entry "undefined: a non-finite value classified instead of called
Invalid" in `mutations.json` and is killed by the run recorded here.

## Reading the verdicts

```text
killed                   the suite failed. The intended outcome.
killed by the COMPILER   the mutant did not build. WEAKER evidence: it shows
                         the code would not compile, not that the tests would
                         notice. Where this was the only outcome the mutation
                         was rewritten to compile and re-run.
*** SURVIVED ***         a gap. Diagnosed, not explained away.
```

## Running it

From the repository root, with `BETTERCAD_BUILD_ROOT` set:

```text
bash docs/verification/P16-QUALITY-001/qualification/mutation/mutate.sh
```

It builds and tests in `debug-ext` only. A mutation that a single preset
cannot distinguish is a different question, and the three-preset qualification
is where that is answered.
