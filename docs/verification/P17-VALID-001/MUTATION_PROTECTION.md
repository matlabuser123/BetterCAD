# P17-VALID-001 — mutation protection

```text
28 probes applied, 27 KILLED, 1 SURVIVED
```

The one survivor is a branch that is **unreachable from a document**, and the
measurement that establishes that is below. Two other probes survived earlier
runs and each exposed something real: one a test gap, one a stale anchor.

## Method

Each probe restores the two production files from hash-verified pristine copies,
applies **one** verified literal substitution (the harness dies if the anchor is
missing or ambiguous), rebuilds, runs this milestone's selection, and restores.
The run ends with a restore build.

```text
filter      StructuralValidation_                 32 tests
harness     .../scratchpad/validmut/probe.sh      batches A-E
driver      .../scratchpad/validmut/all.sh        all five, one invocation
logs        final-batch-A.txt ... final-batch-E.txt, final-restore.txt
run         2026-10-11 00:39 to 01:50, then restore: 32/32 at 01:50:24
```

**ONE DRIVER, ALL BATCHES, NOTHING ELSE TOUCHING THE BUILD ROOT.** The earlier
rounds were run one batch per invocation with the process list checked before
each; the final round uses a single driver that runs all five in sequence, which
removes the window in which a second launch could collide. P17-POST-001's first
attempt ran in the background, its completion notification arrived while the
script was still running, and it spent twenty minutes overwriting source edits
and corrupting the build root. One earlier batch here was launched while another
was still running and did not start, on an unset variable in the detached shell
— the process check caught it before it could collide.

**MUTATIONS MUST COMPILE.** `-Werror=unused-variable` and
`-Werror=unused-but-set-variable` turn "drop a term" into a *compiler* kill,
which says nothing about the tests. M8 was re-run for this reason: its first
form added a duplicate `case` label, which GCC rejects outright. The re-run form
uses an early return and is a real kill, by 5 tests.

**A restored source is not a restored binary.** The harness restores without
rebuilding between probes, so after a batch the binary is the last mutant's. The
driver ends with `probe.sh restore`, which rebuilds, re-runs the selection and
prints the hashes — confirmed at 32/32.

## What the probes ran against, and the two comments that changed after

```text
PROBED    src/structural/StructuralValidation.cpp        3e5acc20ff5936b0ff7dd8f6caed396ed46f7cf6
          include/.../StructuralValidation.hpp           a71542129b07b277fbc42f7c580e3afd0b135703

FROZEN    src/structural/StructuralValidation.cpp        3e5acc20ff5936b0ff7dd8f6caed396ed46f7cf6  (identical)
          include/.../StructuralValidation.hpp           245031486ef2a64bc5c551295eb184cc21b8e23b
```

The `.cpp` is **byte-identical**. The header differs by exactly two doc
comments, both of which had drifted when `QualityPolicyUnusable` became a
warning: one said "the two warnings" when there are three, and one called that
code a refusal reachable only through the public entry point when it is now a
warning reachable from a document. The full diff is in FREEZE.md.

Neither can affect the executable or a test, and the probe results stand: a
mutation probe measures behaviour, and no behaviour changed between the probed
tree and the frozen one.

## Three probes changed the milestone

Recorded first, because they are the most useful thing the probe set did.

```text
M14 / the quality-finding merge        SURVIVED, then fixed, now KILLED

    Dropping the quality stage's findings on their way into the
    analysis-level report changed NOTHING in the whole suite. The gap was
    larger than the probe: no test anywhere checked that a mesh quality
    finding reaches the analysis path at all, because every document fixture
    meshes cleanly and there was never a finding to drop.

    Closed by MEASURING what a document-backed mesh can actually produce.
    A 40 x 30 mm plate at a 20 mm target, by thickness:

        2.0 mm   3r/R 4.62e-03  aspect 20.0   accepted,  0 findings
        1.0 mm   3r/R 5.78e-02  aspect 50.0   accepted,  0 findings
        0.5 mm   3r/R 2.94e-02  aspect 100.0  WARNINGS,  1 finding
        0.2 mm   3r/R 1.19e-02  aspect 250.0  WARNINGS,  2 findings
        0.1 mm   3r/R 1.20e-05  aspect 400.0  WARNINGS,  4 findings

    `CarriesAMeshQualityFindingIntoTheAnalysisReport` uses the 0.5 mm case
    and asserts every mesh-level finding appears in the analysis-level
    report, that the measurements come across too, and that a warning does
    NOT short-circuit the chain.

M5 / the aspect anchor                 ANCHOR FAILED, not a survivor

    An early batch A reported ANCHOR FAILED rather than a result: the policy
    source had changed under it, because the aspect threshold gained a
    failure bound between the probe being written and run. A probe whose
    anchor no longer exists is not a survivor and must not be recorded as
    one. Re-anchored, moved to batch E, killed by 3 tests.

M27 and M28 / the Correction 3 fix     ADDED BECAUSE THE FIX NEEDED THEM

    Correction 3 made `QualityPolicyUnusable` a warning and narrowed the mesh
    stage's short-circuit from "any finding" to "a refusal". Two probes were
    added to hold both halves: M27 makes the code a refusal again, M28
    restores the broad short-circuit. Both are killed. A fix without a probe
    is a fix nobody will notice being undone.
```

## Results

```text
#    Mutation                                      Result   Killed by
----------------------------------------------------------------------------
batch A -- the policy numbers
M1   accuracy floor loosened to 1e-3           KILLED    8 of 32
M2   envelope warning reach cut 100x           KILLED    2 of 32
M3   the radius hard failure removed           KILLED    4 of 32
M4   a SECOND failure bound added, on the      KILLED    3 of 32
     minimum dihedral (the forbidden line)

batch B -- the comparison and the severity map
M6   direction ignored in past()               KILLED   19 of 32
M7   past() made non-strict                    KILLED    1 of 32
M8   accuracy refusal downgraded to a warning  KILLED    5 of 32
M9   the no-load warning made a refusal        KILLED    3 of 32
M10  status() reads only the first finding      KILLED    1 of 32
M11  warnings reported as Accepted             KILLED    6 of 32

batch C -- the staged chain
M12  restraint count bound cut from 6 to 1     KILLED    1 of 32
M13  restraint count check removed             KILLED    1 of 32
M14  quality findings not merged               KILLED    1 of 32
M15  the chain does not short-circuit after   SURVIVED  unreachable
     a quality rejection
M16  the mode material check skipped           KILLED    1 of 32
M17  the no-load warning never added           KILLED    1 of 32

batch D -- the solve entry's gates
M18  the validation gate skipped               KILLED    1 of 32
M19  the equilibrium result ignored            KILLED    1 of 32
M20  a solver failure ignored                  KILLED    2 of 32
M21  nodal reactions dropped                   KILLED    1 of 32
M22  stale inputs not reported                 KILLED    2 of 32

batch E -- the second bound, the observation type, and Correction 3
M23  aspect ceiling removed                    KILLED    2 of 32
M24  aspect ceiling loosened 100x              KILLED    3 of 32
M25  aspect ceiling tightened to 50            KILLED    6 of 32
M5   aspect envelope warning tightened to 8    KILLED    3 of 32
M27  the policy warning made a refusal again   KILLED    2 of 32
M28  the mesh stage short-circuits on ANY      KILLED    1 of 32
     finding, not only a refusal
M26  an observation loses its element handle   KILLED    1 of 32
```

## The survivor, and why it is unreachable rather than untested

```text
M15   the quality stage's short-circuit removed

      report.reachedStage = ...::Quality;
      if (report.status() == ValidationStatus::Rejected) { return report; }
                     ->  if (false && ...) { return report; }
```

For this to be observable, the quality stage must REJECT inside the
analysis-level path. All of its refusals are unreachable there:

```text
MeshStructurallyInvalid   a HELD mesh is never structurally invalid --
MeshHasInvalidElements    `Mesher::generate` refuses one that is not, which
                          is P16's contract and `requireStructuralModel`
                          relies on it rather than re-deriving signed volumes
QualityPolicyUnusable     is now a WARNING, not a refusal, so it cannot
                          reject at any level -- see
                          PRIOR_DRAFT_CORRECTION.md Correction 3
ElementAccuracyBelowFloor needs 3r/R < 1e-10 or aspect > 3e5 from a mesher
                          output. MEASURED: the worst document-backed mesh
                          this milestone could produce has 3r/R 1.20e-05 and
                          aspect 400 -- FIVE ORDERS above the floor and 750x
                          below the ceiling. The worst QUALIFIED reference
                          mesh is 3r/R 3.23e-04 and aspect 80.0.
```

So the quality stage can only produce WARNINGS at the analysis level, and
warnings do not trigger the short-circuit in either the original or the mutant.

**The branch is kept, and the reasons are stated rather than assumed:**

```text
it is correct      if the mesher ever produced a worse mesh, or if a bound
                   moved, stopping before the material and restraint stages
                   is the right behaviour -- the later stages would be
                   reporting consequences of a mesh already refused
it costs nothing   one comparison on a path about to do a factorisation
the same           `StopsAtTheFirstRefusingStage` tests the INPUTS stage
short-circuit IS   short-circuit end to end: a model wrong in three ways at
tested             once reports exactly ONE finding, naming the stale mesh
its SCOPE is       M28 mutates the same statement in the other direction --
tested             broadening it to fire on any finding -- and is KILLED. So
                   the branch's shape is pinned even though its rejection
                   arm is unreachable
the refusals ARE   all of them are reachable and tested through
tested             `validateStructuralMeshQuality`, which takes a report and
                   is public for exactly that reason
```

This is the pattern P16-QUALITY-001 recorded: a surviving mutation can mean the
branch is unreachable rather than that the tests are weak. Recording it as
unreachable with the measurement behind it is the honest answer; deleting the
branch to make the set read 28/28 would remove correct defence for a tidier
number.

## What the probe set establishes

```text
BOTH THRESHOLDS ARE LOAD-BEARING IN BOTH DIRECTIONS
    loosening either (M1, M24), removing either (M3, M23) and tightening
    either (M25, and M1's companion) are all killed -- so neither number can
    drift in either direction without a test failing.

THE FORBIDDEN THIRD LINE IS FORBIDDEN BY A TEST, NOT BY A COMMENT
    M4 adds a failure bound on the minimum dihedral -- the shape-quality
    rejection line the brief warns against -- and is killed.

THE SEVERITY MAP CANNOT SLIP, IN EITHER DIRECTION
    M8 downgrades the accuracy refusal, M9 promotes the no-load warning, M27
    promotes the policy warning. All three killed, so every line between
    "proceeds" and "refused" is pinned from both sides.

THE DERIVED VERDICT CANNOT BE FAKED
    M10 reads only the first finding and M11 reports warnings as Accepted.
    Both killed.

THE COMPARISON IS DIRECTIONAL AND STRICT
    M6 (direction ignored) kills 19 of 32 -- the broadest kill in the set --
    and M7 (non-strict) kills exactly 1: the synthesised boundary test,
    which exists because no measured mesh can land on a bound.

EVERY SOLVE GATE STILL REFUSES
    M18 through M22 each remove one gate from the orchestration and each is
    killed, including the two that cannot be injected from outside and are
    reached by tightening the gate below its measured value.

THE NECESSARY-CONDITION RULE IS PINNED AT A NON-ZERO VALUE
    M12 narrows the restraint bound from 6 to 1 and is killed -- by the
    four-degree-of-freedom case, which exists because the empty-restraint
    case alone would have let M12 survive.
```
