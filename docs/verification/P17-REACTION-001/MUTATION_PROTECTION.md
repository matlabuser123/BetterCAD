# P17-REACTION-001 — mutation protection

```text
20 probes applied, 20 KILLED, 0 SURVIVED
```

One probe survived its first run and the survival was a **test gap, not a
production defect**: every equilibrium case recovered about the global origin,
where `x - O == x`, so a path that ignored the origin was a no-op for all of
them. The gap is closed and the probe is killed. That is the most useful thing
the probe set did.

## Method

Each probe restores the two production files from hash-verified pristine
copies, applies **one** verified literal substitution (the harness dies if the
anchor is missing or ambiguous), rebuilds, runs the reaction selection, and
restores. The run ends with a restore build.

```text
filter      StructuralReaction_     20 tests, 21 after the fixture below
harness     .../scratchpad/reactmut/probe.sh      (batches A-D)
            .../scratchpad/reactmut/probe-e.sh    (re-runs)
            .../scratchpad/reactmut/probe-f.sh    (M13, after the gap closed)
logs        batch-A.txt ... batch-F.txt
```

**ONE BATCH PER INVOCATION, IN THE FOREGROUND**, and the process list checked
before anything else is touched. P17-POST-001's first attempt ran in the
background; the completion notification arrived while the script was still
running and it spent twenty minutes overwriting source edits and corrupting
the build root. The harness also classifies `truncated` and `Permission denied`
as VOID rather than as a compiler kill.

**MUTATIONS MUST COMPILE.** `-Werror=unused-variable` and
`-Werror=unused-but-set-variable` turn "drop a term" into a *compiler* kill,
which says nothing about the tests. Four probes were re-run for this reason —
M9, M10, M13, M14 — and the re-run forms `(void)`-cast what they orphan.

## Results

```text
#    Mutation                                      Result   Killed by
---------------------------------------------------------------------------
M1   reaction read as F - Ku                       KILLED   18 of 20
M2   every mask forced to fixed()                  KILLED    1 of 20
     (so a free residual is reported as a reaction)
M3   the first constrained DOF omitted             KILLED   17 of 20
M4   Ux and Uy reactions swapped                   KILLED   17 of 20
M5   the external load total negated               KILLED   17 of 20

M6   the force equilibrium gate removed            KILLED    1 of 20
M7   the moment equilibrium gate removed           KILLED    1 of 20
M8   the force imbalance forced to zero            KILLED    1 of 20
M9   the force gate loosened to 1e-3               KILLED    1 of 20
M10  the scale replaced by the NET resultant       KILLED    1 of 20

M12  the moment lever reversed (O - x)             KILLED   16 of 20
M13  the external moment ignores the origin        KILLED    1 of 21
     (after a fixture; see below)
M14  momentAbout ignores its origin                KILLED    2 of 20

M15  a shared DOF counted in every owner's bucket  KILLED    1 of 20
M16  the shared aggregate applied to every DOF     KILLED    2 of 20
M17  every multiplicity forced to one              KILLED    1 of 20
M18  the constraint-source check removed           KILLED    1 of 20
M19  the solution-source check removed             KILLED    1 of 20
M20  the four mesh checks removed                  KILLED    1 of 20
M21  the stale-lookup mesh check removed           KILLED    1 of 20
```

## The gates the brief names

```text
M1   THE SIGN. `residual[row]` -> `-residual[row]`, which is the
     `F - Ku` convention the brief's automatic-failure list names first.
     EIGHTEEN KILLS, the largest in the set, led by the fully constrained
     fixture where `R = -F` holds EXACTLY with no factorisation -- a factor
     of -1 that no tolerance can absorb.

M2   THE FREE RESIDUAL READ AS A REACTION. Forcing every mask to `fixed()`
     makes a Ux-only node report three components, two of which are free
     residual. Killed by the partial-support test, which is the only test
     that can see it -- and the reason that test also MEASURES the free
     residual at those rows, to show it is non-zero and therefore that
     reading it would be a real error rather than a harmless one.

M3   ONE CONSTRAINED DOF OMITTED. Seventeen kills: the reaction total is
     short by one component and equilibrium fails everywhere.

M4   X AND Y SWAPPED. Seventeen kills, which is why the fixtures use three
     DISTINCT load components rather than a single axis.

M5   THE EXTERNAL TOTAL NEGATED, the other way to make the sum look right
     while being wrong. Seventeen kills.

M6, M7
     THE TWO GATES REMOVED, separately. One kill each, which is the correct
     shape: each gate has exactly the test written for it -- the one that
     tightens the threshold below the measured error -- and no other test can
     see it. A high count here would have meant the gates overlapped.

M8   THE GATE BYPASSED rather than removed: an imbalance that always reports
     zero passes any threshold. Killed.

M9   THE TOLERANCE INFLATED TO 1e-3, which is brief section 167's explicit
     concern. Killed -- so the threshold cannot be loosened without a test
     noticing.

M10  THE NORMALISATION REPLACED BY THE NET RESULTANT. This is the one the
     pure-couple fixture exists for: with `sum(F_external) = 0` by
     construction, a denominator of `||F_external||` divides by nothing.
     Killed.

M12  THE MOMENT LEVER REVERSED, `O - x` instead of `x - O`, which is the
     `F x r` error in the only form the type system permits. Sixteen kills.
     (A literal `F x r` is not expressible: `momentOf(lever, force)` takes a
     `Translation3D` and a `Force3D`, so the operands cannot be exchanged --
     and `F x r = -(r x F)`, so the negation IS the mutation.)

M18, M19, M20, M21
     THE SOURCE GATES, one kill each. M18 is the one the brief insists on:
     a constraint set that merely DESCRIBES the same mesh is not the set the
     solve reduced with, and attributing reactions through it is how a source
     mismatch hides.
```

## The one that needed work

### M13 — the external moment ignores the origin

It **survived**, and the reason was a gap in my tests rather than anything in
the code being unreachable.

Every equilibrium case in the suite recovered about the global origin
`(0, 0, 0)`, where

```text
x - O  ==  x
```

so removing the subtraction from `assembledMomentResultant` changed **nothing**
for any of them. The two tests that do use a non-zero origin — the pressure
case's centroid section and the origin-shift test — both go through
`PreparedLoads::resultantMomentAbout` or `SupportReactions::momentAbout`, which
is M14's target and was killed.

So no test recovered reactions **about a non-zero origin at all**.

`StructuralReaction_BalancesTheMomentAboutANonZeroOriginToo` was added:
equilibrium is now verified about three origins — the global one, a near offset
`(21, -14, 33) mm`, and a far one `(1.7, -2.3, 0.9) m` — with two guards that
make it bite:

```text
the three external moments must DIFFER       asserted pairwise
the far origin's moment must exceed 10x
  the global one's                           asserted
```

Without those the origin could again be doing nothing. M13 is now **killed**.

This is also what brief section 98 asks for directly: with the force imbalance
already ~0, the moment imbalance must stay ~0 under a change of origin, because
`M'_imbalance = M_imbalance - dO x F_imbalance` and the second term vanishes.

## What the probes could not express

```text
"integrate stress over the support faces instead"
    not expressible: there is no stress integration to disable. A grep for
    `stress`, `Stress6` or `strainFrom` in StructuralReaction.cpp finds
    nothing. Writing one in would be ADDING a second reaction definition,
    not mutating this one

"recompute K u - F differently"
    not expressible: there is no second `K u` product. A grep for
    `stiffness()` in this file finds nothing -- the residual is READ from the
    solve. M1 and M5 are the expressible forms of getting the quantity wrong

"re-integrate pressure or traction a different way"
    not expressible: no load physics exists here. The external total is read
    from the assembled F through the numbering, and a grep for `PressureLoad`,
    `facet` or `area` in this file finds nothing. M5 is the expressible form

"use current loads against an old solved result"
    M18, M19 and M20 are the expressible forms. The external total comes from
    `system.force()` -- the vector that WAS solved -- and `PreparedLoads` is
    used only for the mesh check and as the test-side oracle, so substituting
    a newer load vector would mean substituting a newer SYSTEM, which M19
    catches

"use current remeshed node positions for an old reaction moment"
    M21 is the expressible form: `momentAbout` and `at` refuse a mesh they do
    not describe, and disabling that check lets a remeshed mesh's coordinates
    be used. Killed

"sum per-restraint results to global without de-duplication"
    M15 and M16, from the two sides. Killed

"persist reactions as canonical authority"
    not expressible: nothing here serializes

"parallel-reduce the aggregation nondeterministically"
    not expressible: there is no parallelism and no unordered container in a
    summation path. The multiplicity table is a sorted vector precisely so
    this cannot arise
```

And `M11` — "moment as `F x r`" — is recorded as **mathematically identical to
M12** rather than run: `F x r = -(r x F)`, and `Moment3D` has no unary minus,
so the only expressible form of the sign error is reversing the lever, which
M12 does and which 16 tests kill.

## Restoration verified

```text
source hashes after the final batch, both files restored to pristine
    src/structural/StructuralReaction.cpp                 d2dcadcd
    include/bettercad/structural/StructuralReaction.hpp   196e4b3e
restore build       OK
21/21 tests passed  100%
```

The harness's last action is a restore build, because **a restored source is
not a restored binary**: a harness that restores without rebuilding leaves the
mutant binary in place, and the next failure looks exactly like the fix being
broken.
