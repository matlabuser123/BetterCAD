# P17-POST-001 — mutation protection

```text
30 probes applied, 27 KILLED, 3 SURVIVED
```

All three survivals are guards on branches **no input can reach**, and they are
reported as survivals rather than dressed up as kills. The reason each is kept
anyway is below.

The von Mises probes come first because brief section 41 makes the
plane-stress substitution high priority, and it is the right call: a 2D formula
agrees with the correct one on most of what a test author writes first.

## Method

Each probe restores the two production files from hash-verified pristine
copies, applies **one** verified literal substitution (the harness dies if the
anchor is missing or ambiguous), rebuilds, runs the post selection, and
restores. The run ends with a restore build.

```text
filter      StructuralPost_       41 tests, counted with ctest -N first
harness     .../scratchpad/postmut/probe.sh  (batches A-E)
            .../scratchpad/postmut/probe-f.sh (batch F, re-runs)
logs        batch-A.txt ... batch-F.txt
```

**ONE BATCH PER INVOCATION, IN THE FOREGROUND.** The first attempt used
`nohup ... &`; the completion notification arrived while the script was still
running and it spent twenty minutes overwriting source edits with its pristine
copies, left a mutation applied, and corrupted the build root. See
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) finding I1. The harness now also
classifies `truncated` and `Permission denied` as VOID rather than as a
compiler kill.

**MUTATIONS MUST COMPILE.** `-Werror=unused-variable` turns "drop a component"
into a *compiler* kill, which proves nothing about the tests, so every probe
that drops a term `(void)`-casts what it orphans. M1 was re-run for exactly
this reason: its first form left `dxy` unused and was killed by the compiler.

## Results

```text
#    Mutation                                      Result   Killed by
---------------------------------------------------------------------------
M1   plane-stress von Mises substituted            KILLED   13 of 41
M2   szz omitted from von Mises                    KILLED   11 of 41
M3   tyz omitted from von Mises                    KILLED    8 of 41
M4   tzx omitted from von Mises                    KILLED    9 of 41
M5   shear coefficient 3 -> 1                      KILLED    9 of 41
M6   hydrostatic stress allowed to leak in         KILLED   10 of 41

M7   gamma/2 dropped in tensorOf(Strain6)          KILLED    2 of 41
M8   stress YZ and ZX swapped                      KILLED    4 of 41
M9   ZX mapped to the XY tensor position           KILLED    5 of 41
M10  strain tensor shear halved twice              KILLED    2 of 41
M11  hydrostatic /3 dropped                        KILLED    7 of 41

M12  the descending principal sort removed         KILLED    8 of 41
M13  principal values sorted ascending             KILLED    8 of 41
M14  non-finite principal value accepted           SURVIVED  (unreachable)
M15  the eigensolve status ignored                 SURVIVED  (unreachable)
M16  the ZX component zeroed before eigensolve     KILLED    5 of 41

M17  the solution-source check removed             KILLED    1 of 41
M18  the numbering mesh check removed              KILLED    1 of 41
M19  the material-source check removed             KILLED    2 of 41
M20  Ux and Uy swapped in u_e                      KILLED    4 of 41
M21  the nodal displacement rotated                KILLED    3 of 41
M22  magnitude as |ux + uy + uz|                   KILLED    2 of 41

M23  the kernel displacement check off             KILLED    1 of 41
M24  the kernel strain check off                   KILLED    1 of 41
                                                   (after a fixture; see below)
M25  the kernel stress check off                   KILLED    1 of 41
M26  the kernel geometry refusal off               KILLED    1 of 41
M27  one element silently skipped                  KILLED    5 of 41
M28  one node silently skipped                     KILLED    6 of 41
M29  the nodal finiteness check off                SURVIVED  (unreachable)

M30  the stale-lookup mesh check off               KILLED    1 of 41
```

## The gates the brief names

```text
M1   THE HIGH-PRIORITY PROBE OF BRIEF SECTION 41. The production body is
     replaced with the common plane-stress form
         sqrt(sxx^2 - sxx syy + syy^2 + 3 txy^2)
     THIRTEEN KILLS, the largest in the set. The full-3D fixture, the
     szz-only uniaxial case, every pure shear, the hydrostatic-shift
     invariance, the rotation invariance and every per-element cross-check on
     two solved models all fail at once. There is no way for a 2D formula to
     survive this suite.

M2-M4
     THE COMPONENTS A 2D FORMULA DROPS, removed one at a time rather than
     together, so each is proved to be load-bearing on its own. szz is worth
     the most (11) because it appears in two of the three normal differences.

M5   vm = sqrt(3)|tau| becomes vm = |tau|. Nine kills -- the three pure-shear
     cases, the full-3D fixture and both solved-model cross-checks.

M6   `sxx - syy` becomes `sxx + syy`, which lets the MEAN stress into the
     answer. Ten kills, led by the five hydrostatic states that must give
     exactly zero and the shift invariance that must hold for every q.

M7   THE ENGINEERING-SHEAR CONVENTION, which is the one convention error in
     this milestone that produces plausible numbers: the normal components
     stay correct and every shear term of every principal strain doubles. Two
     kills, and they are the two tests written for it -- the tensor-mapping
     test and the pure-shear principal strain.

M8, M9
     THE SIXTH COMPONENT, misplaced two different ways. Numerically invisible
     -- `zx` and `xz` are the same number -- until something indexes the
     tensor, which is why the mapping test uses six DISTINCT values and
     asserts `at(0,2) != 5.0` rather than only `at(0,2) == 6.0`.

M12, M13
     THE ORDERING, removed and reversed. The diagonal fixture is GIVEN
     ascending on purpose, so an implementation that passed Eigen's own order
     through fails; eight kills each.

M17-M19
     THE THREE SOURCE GATES. One or two kills each, which is the correct
     shape: each gate has exactly the tests written for it, and no other test
     can see it. A high kill count here would have meant the gates overlapped.

M27, M28
     ATOMICITY FROM THE OUTSIDE. A silently skipped element or node is what a
     non-atomic publication looks like to a caller, and the cardinality
     assertions are what notice: five and six kills.

M30  THE ADVERSARIAL FIX OF FINDING P2, probed as soon as it was written. With
     the mesh check disabled, a lookup of `NodeId(1)` against the REMESHED
     mesh succeeds and returns the old mesh's displacement -- a plausible
     number for unrelated material. Killed.
```

## The one that needed work

### M24 — the kernel strain check

It **survived** the first time, and the reason was that no fixture reached the
branch. The 1e300 case reaches the *stress* check: with a tetrahedron ~2.3e-2 m
across, a displacement of 1e300 m gives a strain near 4e301, which is finite,
and a stress near `lambda * 4e301`, which overflows.

The branch was unreachable, not untestable. A fixture was added at **1e307 m**,
where the strain itself is near 4e308 and overflows first:

```text
displacement   1e307 m per corner, each component FINITE (asserted)
strain         ~4e308  ->  infinity
reported       NonFiniteStrain, asserted EXACTLY
```

So the two overflow fixtures now reach two different checks, which is the two
of them being independent rather than one shadowing the other. M24 is killed.

## The three survivals, and why each guard is kept

```text
M14  the non-finite principal check
     A FINITE symmetric 3x3 cannot produce a non-finite eigenvalue, and the
     STRESS finiteness check runs first and catches every state that could
     have reached here. Nothing can make this branch fire.

M15  the eigensolve status check
     SelfAdjointEigenSolver cannot fail on a finite 3x3. It is kept because
     ignoring a library's own failure signal is the mistake ADR-039 was
     written against -- inverted -- and because a future Eigen version or
     platform may report something this code should refuse rather than use.

M29  the nodal finiteness check in `run`'s node loop
     Possession of a `SolvedSystem` proves every displacement is finite
     (ADR-039), so this cannot fire. It is kept because it names the NODE,
     which the element kernel cannot: the kernel re-checks the same values
     and can only say which corner of which element. Better diagnostics for
     a path that a later milestone will open.
```

These are the honest outcome for guards on impossibilities. Deleting them to
reach "30 of 30" would have removed real defence against a milestone that
builds a model from a file, and would have been a worse result reported as a
better one.

## What the probes could not express

```text
"reimplement B incorrectly in POST"
    not expressible: there is no B to mutate. The file makes ONE
    computeTet4Kinematics call and one ->strainFrom, and has zero occurrences
    of a shape gradient, a 1/(6V) or a cofactor determinant. Writing one in
    would be ADDING code, not mutating it -- and M20/M21 already break the
    input to the shared B

"use a separate D convention"
    same: one .stressFrom, and zero occurrences of "lambda", "E * nu" or
    "2 * (1 + nu)". M7 is the expressible form of the shear-convention error

"average element stress to nodes silently"
    not expressible: there is no averaging, smoothing or extrapolation code
    to disable. M27 and M28 are the expressible forms of a result channel
    going wrong

"use the solver's internal permutation as the nodal order"
    not reachable: SimplicialLDLT does not expose its permutation, which is
    itself why it cannot leak. M21 -- rotating the three components of a
    node's displacement -- is the expressible form, and is killed

"deform the mesh to the displaced configuration"
    not expressible: every input is a const reference, so there is nothing to
    write through. The mutation would have to change a signature

"persist a recovered field as authority"
    not expressible: nothing here serializes
```

And several are covered under different names:

```text
"omit szz from von Mises"                    M2
"omit tyz or tzx"                            M3, M4
"plane-stress von Mises"                     M1
"hydrostatic stress contributes to vm"       M6
"treat engineering gamma as tensor shear"    M7, M10
"permute XY/YZ/ZX"                           M8, M9
"forget to sort principal stresses"          M12, M13
"map txy into the wrong tensor location"     M9
"local element order differs from P17-ELEM"  M20
"rebind an old ElementId to a new mesh"      M18, M30
"combine current material B with old u"      M19
"allow a NaN into a published result"        M23, M24, M25
"skip one element"                           M27
```

## Restoration verified

```text
source hashes after the final batch, both files restored to pristine
    src/structural/StructuralPost.cpp                 35fa492e
    include/bettercad/structural/StructuralPost.hpp   6a581a1e
restore build       OK
41/41 tests passed  100%
```

The harness's last action is a restore build, because **a restored source is
not a restored binary**: a harness that restores without rebuilding leaves the
mutant binary in place, and the next failure looks exactly like the fix being
broken. That happened during this milestone, from the orphaned run, and it is
why the check is here and why the hashes above are recorded.

## What changed after the probes, and why the rest were not re-run

```text
after batches A-E      the per-element kernel's strain fixture (a TEST change)
                       the two lookup accessors taking the mesh (finding P2)

re-run in batch F      M1   its mutation changed
                       M24  the new fixture makes its branch reachable
                       M29  re-run to confirm the survival stands
                       M14  likewise
                       M30  new, for the P2 fix

NOT re-run             A, B, C, D. Their anchors sit in vonMisesStress,
                       tensorOf, at, hydrostaticStress,
                       descendingEigenvalues, principalStresses, run's source
                       checks, elementDisplacements and displacementMagnitude
                       -- none of which changed. A test-set change that only
                       ADDS assertions cannot turn a kill into a survival
```
