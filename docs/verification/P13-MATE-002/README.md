# P13-MATE-002 — Mechanical Mates

```text
STATUS:          PASS
BASELINE:        466b301 (P13-MATE-002 authorization), clean,
                 HEAD == origin/main
SCOPE:           four joints -- revolute, slider, cylindrical, planar --
                 through the existing mate model and the existing solver.
                 No joint limits, no motors, no contact, no kinematics, no
                 configurations, no regeneration pipeline.
IMPLEMENTATION:  four mate kinds, one new field pair on the mate, one new
                 projection source on a solver equation. No new solver.
TESTS:           39 new Catch2 cases in 2 files, plus the four joints added
                 to the derivative gate's existing case
EVIDENCE:        this directory
```

## Scope

A joint is named for the freedom it leaves, not the constraint it adds. That
framing is the whole of this milestone: each of the four is defined by its
degree-of-freedom contract, and every test here checks **which** motion
survives rather than how many.

That distinction is not pedantry. A revolute and a slider both leave exactly
one degree of freedom, so any test that counts degrees of freedom passes
equally well on either — including on an implementation that has confused
them.

## Architectural contract

Nothing new solves anything. The four joints compile into the constraint
problem `P13-SOLVE-001` already qualified:

```text
MateDefinition -> constraint problem -> residuals/Jacobian -> solver -> derived transforms
```

What was **not** added, per the milestone's own instruction: no
`MechanicalMateSolver`, no second transform system, no second DOF system, no
second dependency system. The solver gained no new entry point and no new
status. `assembly::solve()` is the same function with the same signature, and
it still takes a `const Document&`.

What was added is two things, both small and both forced by the physics
rather than chosen for convenience:

| Change | Why |
| --- | --- |
| `MateDefinition::a2` / `b2` | A slider needs a roll reference. See the architectural finding below |
| `Equation::projectFrom` | The roll row projects onto the slide axis rather than a complement basis. One field, one branch on rebase, and no new gradient |

### The architectural finding, and what it forced

**A prismatic joint cannot be expressed from one pair of targets, and this is
provable rather than a matter of taste.**

A mate names one piece of geometry on each side, and each carries one
direction. Any condition written over a single direction pair — parallel,
perpendicular, angle — constrains where `R·da` sits relative to `db`. That
fixes at most **two** of a rotation's three degrees of freedom; the turn about
`db` itself is precisely what such a condition cannot see. Positional
conditions between the two origins supply at most three more.

So one target pair affords at most **2 rotational + 3 translational**
constraints. Against the four contracts:

| Joint | Equations needed | Rotational needed | One pair suffices |
| --- | --- | --- | --- |
| Planar | 3 | 2 | yes |
| Cylindrical | 4 | 2 | yes |
| Revolute | 5 | 2 | yes |
| **Slider** | 5 | **3** | **no** |

The three that fit all leave the turn about their axis or normal free, so
they never need the third rotational constraint. The slider is the one that
must remove it, and it is the one thing its axis pair cannot say.

This was raised before implementing, as the milestone's section 7 requires,
rather than worked around. Three candidates were put up:

| Candidate | Rejected because |
| --- | --- |
| Infer the roll from the resolved frame's in-plane axes | `resolvePlane()` returns a `Frame3D`, so the orientation is *there* — but for a face it comes from the surface parameterization, not from engineering intent. A joint whose behaviour depended on it would shift when a face was regenerated. "Do not silently infer geometry" |
| A `CoordinateSystem` target kind resolving to a full frame | Physically the natural reference for a joint, and it would serve all four. But it is a new kind in `core`'s reference vocabulary, touching ADR-004, persistence and resolution — a larger change than the problem needs today |
| **A second target pair on the mate** | **Chosen.** The mate states which two directions fix the roll instead of inferring them. ADR-004's vocabulary is unchanged — still plane, axis, face. Validation and persistence extend rather than change |

The cost is honest and recorded: `MateDefinition` now carries two fields that
exactly one kind uses. Validation makes that asymmetry explicit — a slider
without them is refused, and every other kind carrying them is refused — which
is the same treatment `Fixed` already gets for naming a component instead of
geometry, and `Distance` for its value.

## Revolute

A hinge. Two axes on one line, held against sliding along it.

```text
axis source        the two MateTargets, both MateTargetKind::Axis
origin             each axis' resolved origin
allowed rotation   about the common axis
remaining DOF      1, rotational
```

```text
Parallel             x2    the axes are parallel
OffsetPerpendicular  x2    and meet -- so they are one line
OffsetAlong          x1    and b sits at a fixed place along it
                     = 5 equations, rank 5
```

`OffsetAlong` is the equation a plane-to-plane `Coincident` uses, applied to
an axis pair: `dot(Pb - Pa, Da)` with `Da` the axis direction rather than a
plane normal. The same equation and the same gradient; only the geometry it
is asked about differs.

Measured, from a start 15 mm off the axis, 20 mm along it and turned 30
degrees about it:

| Expected | Measured |
| --- | --- |
| origin pulled onto the axis and to its fixed place | `(0, 0, 0)` |
| axis aligned | `(0, 0, 1)` |
| **the turn untouched — 30 degrees** | `(cos 30, sin 30, 0)` = `(0.8660254, 0.5, 0)` |
| DOF | 1 |

And separately, a start tilted 20 degrees: the tilt is removed, because a
tilt is not a freedom a hinge has.

## Slider

A slide. Two axes on one line, held against turning about it.

```text
axis source        the two MateTargets, both Axis
roll reference     a2 on a's component, b2 on b's, both direction-bearing
allowed motion     translation along the common axis
remaining DOF      1, translational
```

```text
Parallel             x2    the axes are parallel
OffsetPerpendicular  x2    and meet
roll                 x1    and b has not turned about them
                     = 5 equations, rank 5
```

The roll row is the milestone's only new equation:

```text
r = L * cross(Ra, Rb) . D
```

`Ra` and `Rb` are the roll references' directions, `D` the slide axis frozen
at the base and refreshed on rebase — the same mechanism the `Parallel` rows
already use for their complement basis. It is written as a `Parallel` row
whose projection vector is `D` instead, so it is the existing gradient
unchanged: no seventh equation kind, and no new derivative to get wrong.

**Why not `dot(Ra, Rb) - 1`**, which also says "aligned" in one row: its
gradient is `-sin(theta)`, which vanishes exactly where the references line
up — that is, at the solution. The row would go rank-deficient precisely when
satisfied, and the rank analysis would report a degree of freedom the joint
does not have. The cross-product form's derivative is `cos(theta)` there,
which is 1. The distinction is the same one `P13-SOLVE-001` recorded for its
`Parallel` rows, and it is why that formulation was reused rather than
reinvented.

Measured, from a start 12 mm and −5 mm off the axis, 30 mm along it, turned 25
degrees about it:

| Expected | Measured |
| --- | --- |
| off-axis translation removed | `x = 0`, `y = 0` |
| **the slide untouched — 30 mm** | `z = 0.030` |
| axis aligned | `(0, 0, 1)` |
| **the turn removed** | roll reference back on `(1, 0, 0)` |
| DOF | 1 |

### The slider is not a cylindrical joint, measured

The one test that would catch the two being confused. The same geometry under
each:

| | Equations | DOF | The 25-degree turn |
| --- | --- | --- | --- |
| Cylindrical | 4 | 2 | **kept** — `(cos 25, sin 25, 0)` |
| Slider | 5 | 1 | **removed** — `(1, 0, 0)` |

## Cylindrical

A shaft that turns and slides in its bore.

```text
Parallel             x2
OffsetPerpendicular  x2
                     = 4 equations, rank 4
remaining DOF        2: translation along the axis, rotation about it
```

These are the equations a `Concentric` mate already produces — the same
relationship `Concentric` itself has with an axis-to-axis `Coincident`, which
`P13-SOLVE-001` recorded: identical equations, different engineering meaning,
and the model is what carries the meaning.

Measured, from 10 mm and 6 mm off the axis, 40 mm along it, turned 35 degrees:

| Expected | Measured |
| --- | --- |
| off-axis translation removed | `x = 0`, `y = 0` |
| **both freedoms untouched** | `z = 0.040`, roll `(cos 35, sin 35, 0)` |
| DOF | 2 |

## Planar

Two faces that stay in one plane and may slide and spin in it.

```text
Parallel     x2    the normals are parallel
OffsetAlong  x1    and the planes are the same plane
             = 3 equations, rank 3
remaining DOF 3: two in-plane translations, one turn about the normal
```

Measured, from 12 mm and −8 mm across the plane, 45 mm off it, spun 28
degrees:

| Expected | Measured |
| --- | --- |
| **the two in-plane slides untouched** | `x = 0.012`, `y = -0.008` |
| separation removed | `z = 0` |
| **the spin untouched** | `(cos 28, sin 28, 0)` |
| DOF | 3 |

And a start tilted 15 and −10 degrees is pulled flat.

## DOF contracts

Derived by rigid-body reasoning, not read back from the solver. A free
component has 6 unknowns; the solver reports
`degreesOfFreedom = unknowns - rank(Jacobian)`, so each contract is a
prediction about the rank.

| Joint | Equations | Expected rank | Expected DOF | Measured DOF | Result |
| --- | --- | --- | --- | --- | --- |
| Revolute | 5 | 5 | 1 rotational | 1 | PASS |
| Slider | 5 | 5 | 1 translational | 1 | PASS |
| Cylindrical | 4 | 4 | 1 translational + 1 rotational | 2 | PASS |
| Planar | 3 | 3 | 2 translational + 1 rotational | 3 | PASS |

Every one is also checked for `redundant.empty()`: the equation count equals
the rank, so no joint reports a phantom redundancy. That is the rank-honesty
trap `P13-SOLVE-001` hit, and the reason each joint's row count was chosen to
equal its rank rather than written the obvious way.

**The count is the weaker half.** The identity of the surviving motion is
established by the per-joint tables above, each of which starts the component
displaced along a freedom the joint keeps and rotated out of one it removes.

## Reference compatibility

Only ADR-004 reference kinds; no raw face indices, no nearest geometry, no
inference.

| Joint | May relate | Refused |
| --- | --- | --- |
| Revolute, Cylindrical, Slider | two axes | two planes, or a plane and an axis — "relates two axes" |
| Planar | two planes, faces included | two axes — "relates two planes" |
| Slider roll reference | any direction-bearing target | a target on the wrong component; a target equal to the axis |

All four also inherit the existing rules and are tested against them: two
different components, no geometry related to itself, and no value of their
own — a joint is named for its freedom, so a position along that freedom
comes from a `Distance` or `Angle` mate beside it, not from a field on the
joint.

Unchanged from `P13-MATE-001`: a target whose component is gone, or whose
geometry does not resolve, fails the solve as an error rather than becoming a
status.

## Solver constraint mapping

Every equation each joint generates, in full:

| Joint | Kind | Rows | Residual |
| --- | --- | --- | --- |
| Revolute | `Parallel` | 2 | `L (Da x Db) . Ek` |
| | `OffsetPerpendicular` | 2 | `(Pb - Pa) . Ek` |
| | `OffsetAlong` | 1 | `(Pb - Pa) . Da` |
| Slider | `Parallel` | 2 | `L (Da x Db) . Ek` |
| | `OffsetPerpendicular` | 2 | `(Pb - Pa) . Ek` |
| | `Parallel`, projected on the axis | 1 | `L (Ra x Rb) . D` |
| Cylindrical | `Parallel` | 2 | `L (Da x Db) . Ek` |
| | `OffsetPerpendicular` | 2 | `(Pb - Pa) . Ek` |
| Planar | `Parallel` | 2 | `L (Da x Db) . Ek` |
| | `OffsetAlong` | 1 | `(Pb - Pa) . Da` |

No joint manipulates a transform directly. Every one of them is a set of rows
in the same matrix the basic mates contribute to, which is what lets a joint
and a basic constraint be solved together at all.

## Residual validation

At an exactly satisfied state the residual must be zero and the solve must do
no work. Checked for each joint at a state a engineer would call correct:

| Joint | Start | Max residual | Iterations |
| --- | --- | --- | --- |
| Revolute | at the origin, turned 40 degrees | < 1e-12 | 0 |
| Slider | 55 mm along the axis, not turned | < 1e-12 | 0 |
| Cylindrical | both | < 1e-12 | 0 |
| Planar | in the plane, slid and spun | < 1e-12 | 0 |

Perturbation behaviour is the per-joint tables above: a small and a large
translation, a small and a large rotation, and an orientation the joint must
reject are each covered there.

## Jacobian validation

The hard gate, and the only thing that distinguishes a correct joint from one
that converges to a plausible wrong answer.

```text
method     central difference about a zero increment
step       1e-6
gate       relative error < 1e-7
```

| Mate | Max absolute error | Max relative error | Result |
| --- | --- | --- | --- |
| revolute | 3.46945e-12 | 3.46945e-12 | PASS |
| cylindrical | 3.46945e-12 | 3.46945e-12 | PASS |
| planar | 3.46945e-12 | 3.46945e-12 | PASS |
| **slider** | **5.40728e-12** | **5.40728e-12** | PASS |

The slider is the one carrying an equation of its own, and it lands where the
others do — at the truncation floor of a central difference at this step size,
five orders inside the gate. That is what confirms both halves of the roll
row: that freezing the projection axis is consistent between `evaluate()` and
`jacobianAtBase()`, and that the row's gradient was written into the right
components' columns. A row written against the wrong target's columns is an
error of order 1, not 1e-12.

The nine basic mates were re-measured in the same run and are unchanged
(worst 3.72384e-11, the pre-existing axis-to-axis distance case).

These numbers were read by temporarily tightening the gate to force the
measurements out, since the values print only on failure; the file was
restored and verified byte-identical afterwards.

## Analytical DOF validation

Covered by the DOF contracts table above, and by the per-joint motion
identity tables — which is the part section 12 asks for beyond the count:
"verify the free motions correspond to the intended physical joint, not
merely the same number of DOFs".

The slider-versus-cylindrical comparison is the sharpest instance: two
assemblies of identical geometry, differing only in which joint holds them,
measured to leave different motions.

## Solved-transform validation

Every expected transform in this milestone is derived by hand from the
geometry and written into the test as a literal. The per-joint tables record
expected against measured; tolerance is 1e-8 m against a solver that
converges below 1e-9.

One correction belongs here, because it was a genuine error caught by the
tests. The planar combination case expected `y = +15 mm` from a 15 mm
`Distance` between two XZ planes. It measured `-15 mm`. The solver was right:
a `Distance` is signed along the **first** target's normal, and
`PrincipalPlane::XZ` builds its frame right-handed from `-Y` and `X`, so its
normal is `-Y`. The YZ plane's normal is `+X` by the same construction, which
is why the `x` offset in the same test is positive and passed. The expectation
was corrected and the reason recorded beside it.

## Basic-mate combinations

Joints and the basic constraints solve in one system, and the freedoms add up
as they should:

| Assembly | Equations | Expected DOF | Measured |
| --- | --- | --- | --- |
| Revolute + Angle between the two X axes | 5 + 1 | 0 | `FullyConstrained`, 0 |
| Slider + Distance between the two XY planes | 5 + 1 | 0 | `FullyConstrained`, 0 |
| Cylindrical + Distance | 4 + 1 | 1, the turn | 1, turn kept at 35 degrees |
| Planar + 2 Distance + Perpendicular | 3 + 3 | 0 | `FullyConstrained`, 0 |

Each also checks where the component landed, not just the count: the revolute
case turns to the 30 degrees the `Angle` asks for rather than staying at the
50 it started at, and the slider case sits at the 35 mm the `Distance` asks
for.

## Contradictory systems

| Assembly | Expected | Measured |
| --- | --- | --- |
| Revolute + Perpendicular on the same two axes | unsatisfiable | `Inconsistent`, mates named, no transforms |
| Slider + a 40-degree Angle about its own axis | unsatisfiable | `Inconsistent`, no transforms |
| Cylindrical + Concentric on the same axes | satisfiable, duplicated | `OverConstrained`, 1 redundant, residual < 1e-9, DOF 2 |

The third is the distinction that matters: duplicated is not contradictory.
No constraint is silently dropped in any of them, and no partial solved state
is committed — a failed solve returns no transforms at all.

## Unresolved references

A joint whose target does not resolve fails the solve with `NotFound` before
any iteration. It does not guess, does not bind nearby geometry, and does not
reuse a previous resolution: the system is rebuilt on every call.

Measured on a slider whose component was removed: `NotFound`, not a status,
and no result that could be mistaken for a solve.

## Failure atomicity

| Expected | Measured |
| --- | --- |
| canonical placement after a failed solve | unchanged |
| document revision | unchanged |
| transforms returned | none |
| document still usable | yes — removing the contradicting mate solves, to the right answer |

The last row is the half "nothing changed" would not prove.

## Persistence

All four kinds, saved and reloaded:

| Expected | Measured |
| --- | --- |
| `MateId` preserved | ids come back in the same order |
| kind preserved | all four round-trip by name |
| target identities preserved | full `MateDefinition` equality |
| **roll reference preserved** | `a2`/`b2` present, on the right components, naming the X axis |
| dependencies preserved | equal before and after |
| write what was read | byte-identical file |
| a reloaded assembly solves the same | **bit-identical transforms** |

Two negative cases, because persistence is where a joint could quietly become
a different joint:

- **The roll reference is written only for a slider.** Counted rather than
  searched for — `"a2"` appears exactly once in a file holding a revolute and
  a slider. A whole-file test for its absence would prove nothing.
- **A file whose slider has lost its roll reference is refused on load**, not
  loaded into a slider that behaves as a sleeve. Tested by editing the saved
  file to rename the key.

No solver state is persisted. The `.bcad` file is byte-identical before and
after a solve.

## Determinism

| Expected | Measured |
| --- | --- |
| the same assembly solved twice | identical status, iterations, DOF, residual |
| the transforms | **bit-identical** |
| mates added in a different order | the same transforms |
| Debug / Release / Debug-shared | the same 1421 tests, asserting exact values to 1e-8 m |

Bit-identical is claimed only where earned: nothing is seeded, nothing is
cached, and iteration order is fixed by ascending ID.

## Solver regression

The four joints must not weaken what `P13-SOLVE-001` qualified. All 30 of its
cases were re-run against this tree and pass unchanged, covering `Fixed`,
`Coincident`, `Concentric`, `Parallel`, `Perpendicular`, `Distance`, `Angle`,
and the fully-constrained, under-constrained, inconsistent and redundant
classifications.

The nine basic mates' derivative measurements are unchanged from that
milestone's recorded values.

No existing test was modified or deleted. Across the tracked files this
milestone touches, `git diff --numstat` shows 6 deleted lines in total, all of
them source lines replaced in place, and none in a test.

## Adversarial review

Three findings, **no production defect**; nothing weakened. Two of the three
were my own arithmetic, caught by tests that disagreed with it.

### Cleared

| Question | Answer |
| --- | --- |
| Can a revolute leave translation free? | No — measured at `(0, 0, 0)` from a start 15 mm off the axis and 20 mm along it |
| Can a slider allow rotation? | No — measured back at `(1, 0, 0)` from a 25-degree start, and compared against a cylindrical joint on identical geometry that keeps it |
| Can a cylindrical joint constrain its intended rotation? | No — the 35-degree turn survives untouched |
| Can a planar joint constrain in-plane translation? | No — 12 mm and −8 mm both survive |
| Can axis sign reversal change the behaviour wrongly? | No — a revolute with its axis flipped 180 degrees still solves onto the same line. It cannot distinguish the sense, which is recorded as a limitation, not a defect: a hinge does not care which way the pin goes |
| Can flipped plane normals create inconsistent semantics? | The joint accepts either sense, measured. Recorded as a limitation, since a reader may expect contact semantics this mate does not claim |
| Can mate order change the solved state? | No — the same assembly built in two orders gives the same transforms |
| Can component order change the solution? | No — ascending ID order, unchanged from `P13-SOLVE-001` |
| Can redundant equations make a valid joint look over-constrained? | No — every joint asserts `redundant.empty()`; each one's equation count equals its rank by construction |
| Can a joint introduce a singular Jacobian? | Yes, in exactly one way, and it is found and named rather than hidden — Finding 3 |
| Can convergence hide a wrong remaining DOF? | No — DOF comes from the Jacobian rank, not from convergence, and every count is predicted before it is measured |
| Can unresolved targets use stale geometry? | No — the system is rebuilt on every call; a removed component gives `NotFound` |
| Can save/load change target identity? | No — full definition equality, dependencies equal, byte-identical rewrite, and the reloaded assembly solves bit-identically |
| Can derived transforms overwrite canonical placement? | It cannot — `const Document&`, and the file is byte-identical across a solve |
| Can Debug and Release disagree materially? | The three presets run the same tests asserting exact values to 1e-8 m |
| Were any solver tolerances loosened? | No. No tolerance line was changed or removed anywhere; the derivative gate is the same 1e-7 it was, verified byte-identical after the measurement probe |

### Finding 1 — a validation message that masked the rule under test

`MechanicalMate_RelatesTwoDifferentComponents` built its slider case with the
same axis on both sides, so validation refused it as "cannot relate geometry
to itself" — a correct refusal, but a different one from the rule the test
names. The test was asserting a message it was not reaching.

Fixed in the test, by giving the one-component case two distinct axes so it
reaches the component rule. Both refusals are correct; the test now checks the
one it claims to.

### Finding 2 — a hand-derived expectation with the wrong sign

Recorded under solved-transform validation above: a 15 mm `Distance` between
two XZ planes puts the component at `y = -15 mm`, not `+15`, because the XZ
plane's normal is `-Y` under the right-handed frame construction. The
implementation was right and my arithmetic was wrong. The expectation was
corrected, and the convention written into the test beside it so the next
reader does not have to rediscover it.

### Finding 3 — a slider whose roll reference lies along its own axis

The failure this design could plausibly have hidden, and the reason it was
looked for: the roll row is `cross(Ra, Rb) . D`, so roll references parallel
to the slide axis make it identically zero. Five equations are still built,
the row contributes nothing, and the "slider" keeps the very turn it exists to
remove. Anything that counted equations would see 5 and believe it.

Validation cannot catch it — whether two resolved directions are parallel is
geometry, not definition, and checking it would mean resolving geometry inside
a function that deliberately does not.

**What actually happens, measured:** the rank analysis finds the row adds
nothing to the span, names the mate redundant, and reports `OverConstrained`
with 2 degrees of freedom instead of 1. The caller gets a diagnostic pointing
at the mate that contributed nothing, and — because an over-constrained result
carries no transforms — no positions at all. A slide that is not one does not
quietly hand back the sleeve's answer.

**No fix made.** The existing rank machinery already produces the right
outcome, and adding a geometric pre-check for one mate would be a second
validation path for a case the solver already reports correctly. Pinned by a
test and recorded as a limitation.

My first expectation for this case was wrong in a small way worth noting: I
predicted it would return the sleeve's transforms alongside the diagnostic. It
returns none, because `OverConstrained` withholds transforms — `P13-SOLVE-001`
behaviour this milestone inherits. The test now asserts what the contract
actually gives.

## Full regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`; all fifteen are 0.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 434/434 | 0 | **1421/1421** | 221.0 s |
| `release` | 434/434 | 0 | **1421/1421** | 210.9 s |
| `debug-shared` | 434/434 | 0 | **1421/1421** | 191.7 s |

Then the milestone's related tests, five times over until failure — 914 tests
selected by the mechanical, joint, solver, mate, reference, component,
placement, datum, object, persistence, parameter, sketch, CLI and architecture
names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **914/914 ×5** | 711.8 s |
| `debug` | **914/914 ×5** | 775.0 s |

1421 = the 1382 of `P13-SOLVE-001` plus this milestone's 39. The derivative
gate ran in all three presets, `debug-shared` included.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           6ed3059c05c44b56d71c7a104a92028f8eaced6c
src               4af7fd6e2bc1f4dc8faf97422152ee5c81a59490
tests             266c1d3da83e99c191a6bddc11b9223118e6c473
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`ninja: warning: premature end of file; recovering` is the first line of each
build log. It is ninja's own `.ninja_log`, damaged by runs killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 434
targets were built in every preset regardless. It is not a compiler warning,
and no compiler warning appears in any of the three build logs.

**Test discovery, checked rather than assumed.** A new test file that is
compiled but never registered would leave the suite passing and the milestone
unverified. All 39 mechanical-mate cases appear by name in the qualification's
own ctest log, counted from its `Start` lines, and none is reported other than
`Passed`.

## Known limitations

- **A slider's roll reference must not lie along its slide axis.** The roll
  row is `cross(Ra, Rb) . D`, which is identically zero when the references
  are parallel to `D` — the row says nothing and the slide keeps the turn it
  exists to remove. Validation cannot catch this, because whether two
  references are parallel is geometry, not definition. What happens instead is
  that the rank analysis finds the row adds nothing, names the mate redundant,
  and reports `OverConstrained` with 2 degrees of freedom — a diagnostic
  rather than a silent sleeve. Measured, not assumed.
- **An over-constrained assembly returns no transforms**, so the case above
  gives a diagnostic and no positions. That is `P13-SOLVE-001`'s contract,
  inherited here rather than changed.
- **The joints do not distinguish the sense of an axis or a normal.** They are
  built on `Parallel` rows, which are satisfied whichever way round the
  directions point. A revolute hinges the same with its pin reversed, and a
  planar joint accepts faces whose normals oppose as readily as ones that
  agree. Both are measured. A joint that meant "these faces are in contact,
  facing each other" would need a sense-aware formulation, which none of these
  claim.
- **No joint carries a value.** A revolute has no angle and a slider no
  position; those come from an `Angle` or `Distance` mate beside the joint, as
  the combination tests show. Joint limits, motors and driven motion are
  explicitly outside this milestone.
- **`MateDefinition` carries two fields exactly one kind uses.** Validation
  makes the asymmetry explicit in both directions, but it is an asymmetry, and
  a second joint needing a different auxiliary reference would be the point to
  reconsider the coordinate-system target kind that was weighed and set aside.
- **Nothing consumes the solved transforms yet**, unchanged from
  `P13-SOLVE-001`: they are returned, not applied, and no regeneration calls
  the solver.

## Result

```text
TASK:            P13-MATE-002 — Mechanical mates
IMPLEMENTATION:  four mate kinds, a roll-reference field pair on the mate,
                 and one projection source on a solver equation.
                 +42 Mate.hpp, +57/-2 Mate.cpp, +84/-1 SolverSystem.cpp,
                 +10 SolverSystem.hpp, +15/-3 MateJson.cpp.
                 No new solver, no new status, no new entry point.
TESTS:           39 new cases in 2 files (1044 lines), and the four joints
                 added to the derivative gate
VALIDATION:      every expected transform hand-derived; Jacobians against
                 central differences, worst new mate 5.41e-12 against a
                 1e-7 gate; every DOF count predicted before measured
REGRESSION:      1421/1421 on debug, release and debug-shared, each from
                 clean; 914/914 five times over in release and debug;
                 0 compiler warnings in all three builds; all 30
                 P13-SOLVE-001 solver cases unchanged
ADVERSARIAL:     3 findings, 0 production defects
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: the four joints leave the motions they are named for, the
derivatives are right, the degrees of freedom are counted right, and a joint
and a basic constraint solve together in one system — each measured against
something that is not the solver.

What is **not** claimed: that any joint distinguishes the sense of its axis or
normal; that a slider survives a roll reference along its own axis (it is
reported, not solved); that a joint carries a position or a limit; or that
anything consumes the transforms yet.

## Revision

| When | What |
| --- | --- |
| 20:0x | the architectural finding: a prismatic joint needs a third rotational constraint that one target pair cannot supply. Raised before implementing, three candidates weighed, second target pair chosen |
| 20:2x | four joints implemented; derivative gate passed before anything was built on it |
| 20:5x | two hand-derived expectations shown to be wrong, both mine: a validation message and the sign of the XZ plane normal |
| 21:0x | adversarial cases added; the degenerate roll reference behaves as a diagnostic, not a silent sleeve |
| 22:1x | full debug suite 1421/1421; tree frozen and qualified from clean |
| 23:07 | three presets and both repeat stages PASS on the final tree |
