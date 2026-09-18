# Adversarial Review

Run before a substantial milestone is called complete. The aim is to disprove
completion, not to confirm it. Read the final diff, not the intention.

```text
CLAIM BEING CHALLENGED   what "done" is being asserted
ASSUMPTIONS              what the implementation takes for granted
```

Work through these, and answer each with evidence rather than belief:

- What important case is missing?
- Could this pass its tests and still be geometrically wrong?
- Are the expected values independent of the implementation?
- Did a test get weakened, deleted, skipped or renamed away?
- Did a tolerance move, and if so, what measurement justified it?
- Is there hidden global state? Does building A then B differ from B alone?
- Can save → load → regenerate change the result?
- Can undo/redo leave stale bodies or dangling references?
- Can a parameter change leave stale geometry downstream?
- Can a stable reference bind to the wrong face instead of failing?
- Could Debug, Release and Debug-shared differ?
- Could the order of operations change the result?
- Can a failure leave partial state committed?
- Was an architectural boundary crossed, or the scope widened?

```text
FINDINGS         each with a reproducer, or "none found"
REQUIRED FIXES   what must change before [x]
VERDICT          COMPLETE / NOT COMPLETE
```

A credible defect found here is reproduced, given a regression test, fixed at
the root and reverified before `[x]`. Finding nothing is a result too, and is
recorded as one.
