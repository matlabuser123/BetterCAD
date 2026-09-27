# P15-MASS-001 — Adversarial review

**Aim: disprove the milestone.** 24 questions, asked against the final diff.

**Result: 4 findings, all resolved before `[x]`.** Three were defects in this
milestone's own work (one wrong API contract, one comment asserting something
untrue, one test that proved nothing). One is a recorded limitation that cannot be
removed without a distinct type.

Two of the findings were found by tests written before the code they covered, and
one by reading the kernel's header rather than trusting the first result that
looked plausible.

---

## Findings

### F1 — The API claimed the wrong reference point (DEFECT, fixed)

`Body::volumeSecondMoments()`, as first written and first documented, said the
moments were **about the body's own origin**. They are about its **centroid**.

Found by the first test written, before any mass layer existed: a 2 x 3 x 5 mm box
with a corner on the origin, whose about-origin moments (340, 290, 130 with
products -45, -75, -112.5) and centroidal moments (85, 72.5, 32.5 with products 0)
differ by a factor of four. The kernel returned the second set.

The cause was trusting one half of an ambiguous header. `GProp_GProps(const gp_Pnt&
SystemLocation)` documents its argument as the "reference point of the system used
for inertia accumulation", which reads like a choice of frame. `MatrixOfInertia()`
documents its result as being "in the central coordinate system (G, Gx, Gy, Gz),
where G is the centre of mass". Both are in the same header, 90 lines apart.

Measured, the second is the operative one:
`kernel-probe/second-moment-probe-eps1e-10.log` shows the identical matrix for four
different `SystemLocation` values, and for the same box translated 1000 mm.

**Had this shipped**, every inertia BetterCAD reported would have been the
centroidal one labelled as the origin one, and the parallel-axis shift built on top
would have applied Huygens a second time — wrong by exactly `m d^2`, with no
symptom other than a number a user would have to check by hand.

Fixed by renaming the accessor `centroidalVolumeSecondMoments()`, rewriting both
doc comments to state the measured behaviour, and keeping the box fixture as the
regression.

**This also improved the design.** The centroidal tensor is the invariant one, and
every other frame follows from it by an ADDITION. Taking the centroidal tensor from
an about-origin one requires a SUBTRACTION of two nearly equal numbers: for a body
1 m from the origin and 10 mm across, about four digits. So the direction the kernel
forces is the well-conditioned one, and `shiftedFromCentroid()` only ever adds.

### F2 — A comment asserted an untruth about the regenerator (DEFECT, fixed)

`partMassProperties()` checks the feature's `NodeState` and refuses `Failed` and
`Blocked`. The comment justifying it said "a body can survive a later failed pass
-- the regenerator keeps the last good one".

It does not. `tests/features/RegeneratorTests.cpp` states and asserts the opposite:
"Failed and blocked items have no (stale) result", with
`regenerator.body(extrude) == nullptr`.

So the state check is **unreachable today**. Two options: delete it, or keep it and
tell the truth about it. Kept, because it costs one map lookup and what it guards
against is a mass derived from geometry that no longer follows from the document —
worth a lookup even at probability zero. The comment now says it is unreachable,
says which invariant makes it so, and points at the test that asserts that
invariant at the point of use, so the day the regenerator starts keeping stale
bodies is a failing test rather than a silent wrong number.

### F3 — A test took an escape hatch and proved nothing for a whole run (DEFECT, fixed)

The first version of the failed-regeneration test tried to break the feature by
setting its depth to zero, with two `SUCCEED()` branches for the case where that
did not produce a failure.

It took one. `ExtrudeFeature::setDefinition` rejects a zero depth outright, so the
test passed while never reaching a failed regeneration, never reaching
`partMassProperties`, and never checking a diagnostic. It was found by running the
single test with `-s` and reading which assertions actually fired — not by the test
failing, because it did not fail.

Replaced with a test that over-constrains the sketch (two different lengths for one
line, which the solver reports INCONSISTENT and which blocks the extrude), and that
**begins** by asserting the failure happened: `REQUIRE_FALSE(report.succeeded())`
and `REQUIRE(state == Blocked || state == Failed)`. Verified with `-s` that all
eight assertions now fire.

**The general lesson, recorded because it will recur:** a `SUCCEED()` escape hatch
in a test that sets up a hard-to-reach state is indistinguishable from a passing
test. If a state is worth testing, the test asserts that it reached it.

### F4 — `shiftedFromCentroid` cannot detect a non-centroidal argument (LIMITATION, recorded)

Huygens' theorem is stated from the centre of mass. `shiftedFromCentroid(tensor,
mass, to)` silently gives a wrong answer if `tensor` is about anything else, and it
cannot check: `tensor.about` is the caller's *claim* about where it is, and there is
nothing to compare it against.

A distinct `CentroidalInertiaTensor` type would make it impossible. Not done: the
only caller is inside this module, the name says centroid, the doc says so in
capitals, and a second tensor type for one internal call is machinery out of
proportion to the risk. Recorded rather than hidden. If a second caller appears
outside this module, the type is the right answer then.

---

## The 24 questions

### Correctness of the physics

**1. Is the reference point what we claim?** Now yes; it was not. See F1. Measured
three ways: four `SystemLocation` values give one answer
(`second-moment-probe-eps1e-10.log`); a box translated 1000 mm gives unchanged
moments (same log, and `..._AreAboutTheCentroidNotTheOrigin`); and the corner box's
values match the centroidal closed form to 3e-16, not the about-origin one.

**2. Is the sign convention what we claim?** Yes, measured. **No symmetric body can
answer this** — about its own centroid a box, a cylinder and a sphere all have three
zeros where the products go, which is why the first three fixtures written said
nothing about it. The fixture that answers it is three fused boxes arranged to be
symmetric about no plane through their centroid, whose hand-computed products are
`integral xy dV = -6`, `xz = -9`, `yz = +6` mm^5. The kernel returned `+6, +9, -6`
to 3e-15 — the negated products, i.e. the inertia TENSOR convention. Under the
positive-products convention every sign would be opposite.

No rotation is involved in that fixture, deliberately: a rotated-box fixture would
have confounded the sign under test with the handedness of the rotation, since
`sin(-t)cos(-t) = -sin(t)cos(t)`.

**3. Is the parallel-axis shift's off-diagonal sign right?** Yes. The tensor's
point-mass term is `-m dx dy`, not `+m dx dy`, and the reference cuboid pins it
with integers: `xy` about the origin is `0 - 0.24 x 10 x 15 = -36` kg mm^2 exactly.
A `+` would give `+36`, which the test rejects by value and again by sign.

**4. Could the tests pass with two components swapped?** No. The reference cuboid's
diagonal is 68 / 58 / 26 and its products are -36 / -60 / -90 — six distinct values,
so any transposition fails. The staircase has `xy = +6` and `yz = -6`, opposite in
sign, so an `xy`/`yz` swap fails there even though the magnitudes match.

**5. Are the expected values independent of the code?** Yes. Every one is a
closed-form integral evaluated by hand and written in the test beside the integral
it came from. None was produced by running BetterCAD and recording the output. The
dimensions of the reference fixture (20 x 30 x 50 mm at 8000 kg/m^3) were chosen so
that all ten expected numbers are integers, which makes an arithmetic slip in the
test itself visible.

One test does compare two BetterCAD paths — the kernel integrating a rotated body
against `transformed()` rotating an integrated tensor. That comparison IS the claim
under test, the two paths share no arithmetic, and both sides are separately pinned
to closed forms elsewhere in the same file.

**6. Is there a check that does not depend on the arithmetic being tested?** Four.
The inertia triangle inequality (`Ixx + Iyy >= Izz` on every pairing) is a property
of any physical tensor. The three similarity invariants of `A I A^T` — trace, sum of
principal minors, determinant — are checked under an arbitrary rotation about an
arbitrary axis, and need no closed form for the result at all. A cylinder's `xx` and
`yy` are exactly equal by symmetry, so `|xx - yy| / xx` is an error estimate needing
no reference value. And a sphere's three diagonal components must agree.

**7. Does a void subtract?** Yes, tested on a 40 x 40 x 10 plate with a 5 mm hole
whose axis passes through the plate's own centroid, so the moments subtract with no
parallel-axis term and the subtraction is the only thing under test. The volume is
checked in the same test, because an inertia that subtracted a void it had added
would otherwise be wrong in a way the diagonal might not reveal.

**8. Does a multi-solid body report the whole body?** Yes, tested on two disjoint
2 mm cubes, with `REQUIRE(topology().solids == 2)` first so the fixture is only
about two solids if there are two. The moments are about the combined centroid, and
the test closes with `CHECK(xx > 100)` because one cube alone would read 16/3.

### Numerical quality

**9. Did we loosen a tolerance to pass?** No — we TIGHTENED the product's. The
kernel's second moments were failing a 1e-12 test at 8.3e-10 relative on a cylinder.
Rather than widen the test, the probe measured why: the error TRACKS the requested
integration tolerance (3e-6, 6e-8, 8e-10, 9e-12, 7e-14 at eps 1e-6 to 1e-14), where
the same request overshoots massively for volume. So second moments got their own
constant, `kSecondMomentRelativeTolerance = 1e-14`.

The tests then needed **no new tolerance at all**: every fixture, including the
fused staircase and the two-solid body, passes at the repository's existing
`test::kRelTight` (1e-12). An earlier draft carried a looser `kRelFused = 1e-11`
for fused bodies "as headroom"; it was measured to be unnecessary and deleted. No
tolerance anywhere in this milestone is looser than one that already existed.

**10. Is the new tolerance's cost acceptable, and measured?** Measured, not assumed:
`kernel-probe/eps-cost-probe.log`. x1.17 on a cylinder, x1.61 on a sphere, x1.45 on
a torus, x1.41 on a 13-face plate with a boss and five holes (9.0 ms to 12.7 ms).
Four orders of magnitude of accuracy for under half again the integration cost, on a
call that happens when a user asks for a mass, not on every regeneration.

**11. Is the tighter tolerance still deterministic?** An adaptive quadrature driven
near machine precision could fail to converge or hit an iteration cap. Probed
explicitly: bit-for-bit identical over ten runs at eps 1e-14 on a cylinder, a
sphere, a torus, a box and the 13-face plate. Also asserted in the test suite, eight
repeats, `operator==` on the whole struct.

**12. What is the error budget, and is the test tolerance justified by it?** Yes:

| Body | measured relative error | note |
| --- | --- | --- |
| planar faces (boxes, 0.2 mm to 5000 mm) | <= 5.0e-16, often exactly 0 | quadrature exact |
| spheres (r 0.5 to 500) | <= 6.0e-16 | |
| fused staircase (3 boxes) | <= 3.0e-15 | |
| cylinders, typical (r 2 h 6, r 10 h 30) | 7.2e-14 | |
| cylinders, worst (r 250 h 5) | 2.9e-13 | a flat disc, badly conditioned |
| test tolerance used | 1e-12 | `test::kRelTight`, already in the repository; 3.5x the worst measured |

**13. Could Debug and Release differ?** The integration is floating-point and `-O2`
may reassociate. Answered by qualification, not by argument, and the answer is NO:
all three presets (Debug, Release, Debug-shared) run the full suite and all three
pass it.

That is a genuine cross-preset comparison rather than three independent runs, because
these tests assert **absolute hand-computed values** at 1e-12 rather than comparing
one build's output with its own. Debug and Release both matching 68 / 58 / 26 kg mm^2
and `xy = +6` mm^5 to 1e-12 means the two agree with each other to at least that.
32903 test executions, 0 failures. (This harness has no separate cross-preset value
dump; P12-SKETCH-002 had one because its subject was exported file text, where no
in-test absolute assertion was available.)

### State, persistence and staleness

**14. Can a mass go stale in a file?** No, because none is ever in one. Nothing
derived is persisted: `grep -riE "massmoment|volumesecondmoment|inertia|centreofmass"
src/io/` returns nothing, and the only `mass` in `src/io/` is a parameter's
dimension EXPONENT, which is pre-existing. Four compile-fail cases prove the
absence from the other side: there is no `Document::setMass`, no `Document::mass`,
no `PartMassProperties::setMass` and no `Body::mass`.

**15. Is there hidden global state or a cache?** No statics, no mutable members, no
memoisation. `partMassProperties` takes `const Document&` and `const Regenerator&`
and returns a value. Two tests establish there is no cache from the outside: an
edited density changes the answer with NO regeneration (nothing to invalidate,
because nothing is stored), and repeated calls on unchanged inputs are bit-identical.

**16. Can a parameter change leave stale geometry behind the mass?** For an ordinary
parameter or feature edit, no — tested: editing the extrude's depth and regenerating
moves volume, mass, centroid and inertia together.

**For a CONFIGURATION override, YES, and that is a carried defect this milestone
refuses to answer over.** A configuration changes a parameter's *effective* value
without changing the parameter object, so `features::Regenerator` never marks the
features that read it dirty and their bodies remain the base configuration's.
`m = rho V` would then multiply a correct density by the wrong body's volume and
report it as fact. `partMassProperties` therefore refuses while a configuration with
parameter overrides is active, naming the configuration and the reason, and it
checks this BEFORE anything else so that no amount of valid material and valid
geometry can talk it into answering.

The test asserts the REFUSAL and deliberately does not record what the stale volume
is: pinning that would turn a defect into a contract. See KNOWN LIMITATIONS.

**17. Does the guard over-refuse?** It is narrow on purpose and tested from both
sides. A configuration with no parameter overrides cannot make geometry stale, and
is allowed (`..._AnswerUnderAConfigurationThatOverridesNoParameter`). Deactivating
the configuration restores the answer unchanged, so the guard is about the active
configuration and not a latch that poisons the document.

It does over-refuse in one case: a configuration overriding a parameter that no
feature reads. Narrowing further would mean per-feature dependency analysis, which
IS the unauthorized fix. Recorded.

**18. Can a failure leave partial state committed?** No. `partMassProperties` writes
nothing to the document under any path; every failure returns `std::unexpected`
before any value is assembled. There is no partially filled result to observe,
because the struct is built only after every input has been obtained.

**19. Can undo/redo change the result?** Nothing here is stored, so there is no
state for undo to desynchronise from. Mass follows whatever the document currently
says, in both directions.

### Diagnostics

**20. Is a mass of zero ever returned for a question that was not answered?** No.
Every failure path returns an error. Three distinct material faults — nothing
assigned, the assigned material deleted, the material states no density — are
asserted **pairwise distinct in a single test**, so making two of them identical
fails here rather than passing three tests that each look only at their own. Each is
also checked to name the part or the material and to be more than 20 characters,
which rules out a bare code.

**21. Can a zero or negative density or volume slip through?** `requireDensity`
(P15-MECH-001) refuses non-finite and non-positive densities. This milestone
additionally refuses a non-positive or non-finite volume, and the geometry layer
refuses an empty body and one enclosing no volume.

**22. Does the diagnostic distinguish "broken" from "never produces a body"?** Yes.
A sketch has no body and never will, so telling the user to regenerate it would be
wrong. The message names the object, its type and its regeneration state, and the
test asserts both "no body" and the object's name.

### Architecture and scope

**23. Did we cross an architectural boundary or widen the scope?** No. `features`
(2) includes only `core` (0) headers. OCCT stays in `src/core/geometry/occt/`. The
new dimensions went into `core/units` beside the existing ones and are COMPOSED from
them (`mass * area`, `volume * area`) rather than declared, so the composition is
the proof. No second parameter system, no GUI-owned state, no back door, and no
public API changed except the one rename this milestone introduced in the same
commit. `architecture.layering` passes.

Scope: the regeneration defect was NOT fixed, because it is not authorized. Assembly
aggregation was NOT implemented — see KNOWN LIMITATIONS for the decision and its
reason. Nothing outside P15-MASS-001 was touched except
`kSecondMomentRelativeTolerance`, which is new rather than a change to an existing
value.

**24. Is anything here only there for the tests?** No. Nothing is public solely for
testing, nothing takes a test-only parameter, and no back door exists: the tests
call `partMassProperties`, `shiftedFromCentroid` and `transformed` exactly as any
consumer would. `shiftedFromCentroid` and `transformed` are public because an
assembly aggregation and a drawing note will need them, not because a test does.
