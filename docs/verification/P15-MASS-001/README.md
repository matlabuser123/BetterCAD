# P15-MASS-001 — Mass properties

**TASK** — mass, centre of mass and inertia, derived from a body's geometry and its
part's material. Authorized by the P15-MASS-001 brief; shape decided by ADR-026
(mass is derived, never stored) and ADR-027 (unknown is not zero).

**RESULT: PASS.** Qualified on the first attempt across three presets, 2531/2531
each, 0 warnings, 32903 test executions, 0 failures. Full detail at the end.

---

## SCOPE

Implemented:

```text
a second moment of VOLUME from the kernel, about a body's centroid    core/geometry
the dimensions kg m^2 and m^5, and four units for them               core/units
mass = density x volume, centre of mass, inertia tensor              features
the inertia tensor about the centroid AND about the origin           features
Huygens' parallel-axis theorem from the centroid to any point        features
A I A^T for any rigid motion, rotation or reflection                 features
transformed mass properties of a moved body                          features
five distinct failure diagnostics, never a mass of zero              features
```

**Not implemented, deliberately, and each said plainly:**

- **The carried regeneration defect is NOT fixed.** It is a regeneration concern
  awaiting its own scope decision (TODO.md), not a materials one. Instead, mass
  properties REFUSE to answer while a configuration with parameter overrides is
  active, because the geometry in hand would be the base configuration's and
  `m = rho V` would report a wrong mass as a right one. See KNOWN LIMITATIONS.
- **Assembly aggregation is NOT implemented.** A scope decision, with a reason —
  see KNOWN LIMITATIONS. The pieces it will need (`transformed`,
  `shiftedFromCentroid`) are here and tested.
- P15-CUSTOM-001 and everything after it was not started.

## BASELINE

The audit found what was already there and what was missing.

```text
geometry::MassProperties      volume, surfaceArea, centerOfMass    -- and NO mass,
                              volumeRelativeError, areaRelativeError  deliberately
                                                                      (ADR-026)
second moments of any kind    0 hits. Nothing in the tree integrated one.
Density                       exists, kg/m^3, with _kg_per_m3 and _g_per_cm3
requireDensity(document, id)  exists (P15-MECH-001): refuses unknown, non-finite
                              and non-positive, with a diagnostic naming the material
requireEffectiveMaterial      exists (P15-ASSIGN-001): distinguishes "nothing
                              assigned" from "the assigned one is gone"
RigidTransform3D              exists, with matrix(), apply() and reversesOrientation()
geometry::transformed(body, m) exists -- used here as an INDEPENDENT path
```

So the material half was complete before this milestone began, and nothing here
duplicates it: `partMassProperties` composes `requireEffectiveMaterial` and
`requireDensity` and adds no material logic of its own.

**Two things had to be established before any of it could be built**, because the
kernel's own header does not answer them and getting either wrong is silently wrong:
the reference point of `GProp_GProps::MatrixOfInertia()`, and its sign convention.
See INDEPENDENT VALIDATION.

## ARCHITECTURE

```text
core/units      dimensions::massMomentOfInertia = mass * area     (kg m^2)
                dimensions::volumeSecondMoment  = volume * area   (m^5)
                units kg_m2, kg_mm2, m5, mm5, all four in the catalog
core/geometry   struct VolumeSecondMoments   six components, no reference point
                Body::centroidalVolumeSecondMoments()
features        struct InertiaTensor         six components AND the point they are about
                struct PartMassProperties    the derived result
                partMassProperties(document, regenerator, feature)
                shiftedFromCentroid(centroidal, mass, to)
                transformed(tensor, motion) / transformed(properties, motion)
```

The dimensions are **composed from the existing ones** rather than declared, so the
composition is the proof: `Density * VolumeSecondMoment` IS `MassMomentOfInertia`,
checked by `static_assert` rather than asserted in a comment.

Three decisions worth stating.

**The geometry layer returns a second moment of VOLUME, not of mass.** It has no
density and must not pretend to. `core/geometry` therefore gains no knowledge of
materials, and the multiplication that turns geometry into mass happens in one place,
in `features`, where the material lives.

**The reference point travels with the tensor.** `InertiaTensor` carries `about`,
because a tensor and the point it is taken about are one fact, and pairing a tensor
with the wrong reference point is a defect that no dimension check and no
plausibility check would catch. There is deliberately no rotation field: the axes are
always the document's, so a tensor cannot claim axes that something else would have
to be trusted to honour.

**The centroidal tensor is the primary one, and every other frame is derived from it
by ADDITION.** This was forced by the kernel and turned out to be the better design:
Huygens from the centroid outwards only adds, so no significance is lost however far
the body is from the point. The reverse direction subtracts two nearly equal numbers
and would lose about four digits for a body 1 m from the origin and 10 mm across.

No ADR. This milestone made no architecturally significant decision that ADR-026 and
ADR-027 had not already made; adding one would be ADR noise.

## BLAST RADIUS

```text
direct       Body.hpp, a header every layer above core includes
             Units.hpp and Dimension.hpp, likewise
             UnitCatalog.cpp, whose size is asserted and whose contents the
               parameter READER consults by symbol on load
indirect     every consumer of geometry::Body -- drawings, STEP export, the
               reference models, the CLI -- through the changed header
persistence  nothing new is written; a PARAMETER in one of the four new units
               now round-trips, which is tested
regeneration untouched: partMassProperties is a pure read and registers no handler
CLI          untouched; no command was added (P15-CLI-001)
```

`Body.hpp` gained a type and an accessor, and `OcctBody.cpp` gained a second
integration tolerance used only by the new call. Volume and area are untouched by
construction — but "by construction" is an argument, so the regression set is the
whole suite, three presets, five repeats.

## IMPLEMENTATION

247 lines changed across 11 files, plus 1701 new: 403 of product code and header,
1298 of tests.

`partMassProperties` in order, and the order matters:

```text
1  the active configuration's overrides    -> refuse before anything else, so no
                                              amount of valid material and valid
                                              geometry can talk it into answering
2  requireEffectiveMaterial                -> ADR-026's diagnostic
3  requireDensity                          -> ADR-027's diagnostic
4  the object exists                       -> NotFound
5  the body exists                         -> names the object and its state
6  the feature is not Failed or Blocked    -> unreachable today; see the review
7  volume is positive and finite
8  the centroidal second moments
9  mass = density x volume, and
   aboutCentreOfMass = density x moments, and
   aboutOrigin = Huygens(aboutCentreOfMass, mass, origin)
```

Nothing is written to the document on any path, so no failure can leave partial
state: every error returns before a result is assembled.

## TESTS

52 added: 36 unit tests (11 geometry, 25 features), 15 compile-fail cases and one
persistence round trip. The suite went from 2479 to 2531, and all three presets
discover the same 2531.

```text
tests/core/geometry/VolumeSecondMomentsTests.cpp   11  the kernel's contract
tests/features/MassPropertiesTests.cpp             25  the derived result
tests/io/DocumentFileTests.cpp                     +1  the new units round-trip
tests/compile_fail/MassMisuse.cpp                  15  misuse is rejected
```

The reference fixture is a 20 x 30 x 50 mm box at 8000 kg/m^3, chosen so that **all
ten expected numbers are integers** — V = 30000 mm^3, m = 0.24 kg, cm = (10, 15, 25)
mm, centroidal 68 / 58 / 26 kg mm^2, about the origin 272 / 232 / 104 with products
-36 / -60 / -90. Integers make an arithmetic slip in the test itself visible, and six
distinct values mean no transposition of two components can pass.

The 15 compile-fail cases split in two. Eight keep a second moment of VOLUME apart
from one of MASS — they differ by a density, and by nine orders of magnitude in a
millimetre model. Four prove ABSENCES: there is no `Document::setMass`, no
`Document::mass`, no `PartMassProperties::setMass` and no `Body::mass`, which is how
ADR-026's "never stored" is kept rather than merely intended.

## INDEPENDENT VALIDATION

Every expected value is a closed-form integral evaluated by hand and written in the
test beside the integral it came from. **None was produced by running BetterCAD.**

```text
box, three distinct edges        V(b^2+c^2)/12 on each axis; products 0 by symmetry
cylinder r 20 h 60              V r^2/2 axial; V(r^2/4 + h^2/12) transverse
sphere r 3                      (2/5) V r^2 = 8 pi r^5 / 15
staircase, 3 fused boxes        composed by Huygens from each box's own moments
two disjoint cubes              likewise, as one body of two solids
plate with a central hole       the plate's moments MINUS the void's
rotation by 30 deg about Z      c^2 A + s^2 B, s^2 A + c^2 B, sc(A - B)
translation to (110, 215, 25)   Huygens, all integers
```

Plus four checks that need no reference value at all, so they cannot share a mistake
with one: the inertia triangle inequality (`Ixx + Iyy >= Izz` on every pairing); the
three similarity invariants of `A I A^T` (trace, sum of principal minors,
determinant) under an arbitrary rotation about an arbitrary axis; a cylinder's `xx`
and `yy`, which are exactly equal by symmetry; and a sphere's three equal diagonal
components.

### The kernel probes

Two questions the kernel's header does not answer, measured rather than assumed.
`kernel-probe/` holds the probe sources and their logs.

**The reference point is the CENTROID, whatever location the framework is given.**
`GProp_GProps(const gp_Pnt&)` describes its argument as the "reference point of the
system used for inertia accumulation", which reads like a choice of frame;
`MatrixOfInertia()` says its result is "in the central coordinate system (G, Gx, Gy,
Gz), where G is the centre of mass". The probe gives the identical matrix for four
different locations, and for the same box translated 1000 mm.

**The first draft of this milestone believed the first reading**, documented the
accessor as being about the body's origin, and was caught by the first test written —
before anything was built on it. See ADVERSARIAL_REVIEW.md, F1.

**The sign convention is the inertia TENSOR's: the off-diagonals are NEGATED
products.** No symmetric body can show this, because about its own centroid a box, a
cylinder and a sphere all have three zeros where the products go. The fixture that
answers it is three fused boxes symmetric about no plane through their centroid, with
hand-computed `integral xy dV = -6`, `xz = -9`, `yz = +6` mm^5. The kernel returned
`+6, +9, -6` to 3e-15. A rotated-box fixture would have been useless here: it would
have confounded the sign under test with the handedness of the rotation.

### The error budget

The kernel's volume is exact to ~5e-15 at the repository's existing tolerance of
1e-10, because a volume integral over a planar or quadric face is of low enough
degree that the quadrature is exact. A second moment over the same face is two
degrees higher, and there the achieved error TRACKS the request instead of beating
it:

```text
requested eps   1e-6    1e-8    1e-10   1e-12   1e-14
cylinder error  3e-6    6e-8    8e-10   9e-12   7e-14
```

So 1e-10 would have given an inertia good to nine digits where fourteen were
available. Second moments therefore got their own constant,
`kSecondMomentRelativeTolerance = 1e-14`, separate from
`kPropertyRelativeTolerance` rather than replacing it: volume and area are already
exact at 1e-10, so tightening theirs would buy nothing and slow every existing
caller.

At 1e-14, measured over four decades of size:

| body | measured relative error | note |
| --- | --- | --- |
| planar faces (boxes 0.2 mm to 5000 mm) | <= 5.0e-16, often exactly 0 | quadrature exact |
| spheres (r 0.5 to 500) | <= 6.0e-16 | |
| fused staircase (3 boxes) | <= 3.0e-15 | |
| cylinders, typical (r 2 h 6; r 10 h 30) | 7.2e-14 | |
| cylinders, worst (r 250 h 5) | 2.9e-13 | a flat disc, badly conditioned |
| **tolerance used in the tests** | **1e-12** | `test::kRelTight`, already in the repository |

**No tolerance was loosened anywhere.** The product's was tightened by four orders of
magnitude, and the tests needed no new constant at all: every fixture, including the
fused bodies, passes at the repository's existing `kRelTight`. An earlier draft
carried a looser `kRelFused = 1e-11` "as headroom"; it was measured to be
unnecessary and deleted.

## FAILURE PATHS

Five, each with its own diagnostic, and **never a mass of zero**:

```text
no material assigned              ADR-026; names the part
the assigned material deleted     names the dangling MaterialId
the material states no density    ADR-027; names the material
the object produces no body       names the object, its type and its state
the volume is not positive        names the object
```

The first three are asserted **pairwise distinct in one test**, so collapsing two of
them into one message fails there rather than passing three tests that each look only
at their own. Each is also checked to be longer than 20 characters, which rules out a
bare code.

`unknown != 0` is the rule these enforce. A mass of zero is a physically meaningful
answer, and returning it for a question nobody answered would be worse than failing.

## PERSISTENCE

**Nothing derived is persisted, and nothing can be.**

```text
grep -riE "massmoment|volumesecondmoment|inertia|centreofmass|centerofmass" src/io/
    -> no hits
```

The only `mass` anywhere in `src/io/` is a parameter's dimension EXPONENT, which is
pre-existing. Four compile-fail cases prove the absence from the other side: there is
no setter to persist from.

What IS new to persistence is that a user can now express a PARAMETER in kg mm^2 or
mm^5. That round trip is tested end to end — dimension, SI value, display unit
symbol and display value — because a unit added to `Units.hpp` but not registered in
`UnitCatalog.cpp` would SAVE and then fail to LOAD, which only a persistence test
reaches.

## DETERMINISM

An adaptive quadrature driven near machine precision could fail to converge or hit an
iteration cap, so this was probed rather than assumed: **bit-for-bit identical over
ten runs at eps 1e-14** on a cylinder, a sphere, a torus, a box and a 13-face plate
with a boss and five holes (`kernel-probe/eps-cost-probe.log`).

In the suite, two determinism tests use `operator==` on the whole struct over eight
repeats. No unordered container, hash, pointer ordering, wall clock, locale or random
seed is involved anywhere in the new code.

Whether Debug and Release agree is answered by the three-preset qualification below,
not by argument.

## PERFORMANCE

The tighter tolerance's cost was measured before it was chosen
(`kernel-probe/eps-cost-probe.log`), 1e-10 to 1e-14:

```text
cylinder r 2 h 6              x1.17
sphere r 3                    x1.61
torus R 20 r 5                x1.45
box 10^3                      x1.21
plate, boss and 5 holes       x1.41   (9.0 ms -> 12.7 ms, 13 faces)
```

Four orders of magnitude of accuracy for under half again the integration cost, on a
call that happens when a user asks for a mass — not on every regeneration, which
does not take second moments at all.

## KNOWN LIMITATIONS

**1. Mass properties refuse to answer under a configuration with parameter
overrides.** This is a guard over a CARRIED DEFECT, not a property of mass. A
configuration override changes a parameter's effective value without changing the
parameter object, so `features::Regenerator` never marks the features that read it
dirty and their bodies stay the base configuration's. ADR-026 says a configuration
changes a part's dimensions "through geometry, which is exactly the existing
derivation chain and requires nothing new" — and today that chain is broken.

Refusing is the only honest answer available from this layer. The alternative is a
mass that is wrong without saying so, which is the failure mode this whole milestone
exists to avoid.

The test asserts the REFUSAL and deliberately does not record what the stale volume
is: pinning that would turn a defect into a contract. When the regeneration defect is
fixed, the guard and its test are deleted together, and a test that the mass FOLLOWS
a configuration switch replaces them.

It over-refuses in one case: a configuration overriding a parameter no feature reads.
Narrowing further needs per-feature dependency analysis, which IS the unauthorized
fix.

**2. Assembly aggregation is not implemented.** A scope decision with a reason, not
an oversight. ADR-026 states the consequence that forces it: an assembly's parts all
answer from their own material, part references are internal in P13 (ADR-003 defers
external ones), and a document holds ONE material assignment. So every occurrence in
a single-document assembly necessarily shares one material, and an "assembly mass"
would be `rho x sum(V_i)` — a number no engineer can use for a real assembly, where
a steel bracket and an aluminium plate are the whole point.

Building it would mean shipping something misleading. The pieces it will need are
here and tested: `transformed(PartMassProperties, RigidTransform3D)` for an
occurrence's placement, and `shiftedFromCentroid` for the aggregation. It becomes
worth having when per-occurrence or per-part materials arrive with external
references.

**3. `shiftedFromCentroid` cannot detect a non-centroidal argument.** Huygens is
stated from the centre of mass; the function gives a wrong answer silently if its
argument is about anything else, and cannot check, because `about` is the caller's
claim about where it is. A distinct `CentroidalInertiaTensor` type would make it
impossible. Not done for one internal caller; recorded rather than hidden. If a
caller appears outside this module, the type is the right answer then.

**4. Bodies with a general swept-curve face are refused, not answered.**
`massProperties` integrates those faces itself because the kernel's volume for them
is wrong by up to 4.5e-2 (P12-SKETCH-002), and that integration produces a volume and
a first moment only. Taking SECOND moments from the kernel for exactly the shape class
it is not trusted on would be inconsistent, so
`centroidalVolumeSecondMoments()` refuses with a diagnostic saying so. Reached by an
elliptic prism in the tests, which confirms the body's VOLUME still works — it is a
refusal of second moments alone.

**5. Second moments are available only for a whole body, not per solid or per face.**
Consistent with ADR-024 and ADR-026: neither a solid nor a face has persistent
identity, so neither can be addressed. A two-solid body reports the combined body
about the combined centroid, which is tested.

## ADVERSARIAL REVIEW

**PASS — 24 questions, 4 findings, all resolved before `[x]`.** Three were defects in
this milestone's own work; one is a limitation recorded above. Full text:
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
F1  the API claimed the wrong reference point   DEFECT, fixed (and improved the design)
F2  a comment asserted an untruth about the
    regenerator's stale-body behaviour          DEFECT, fixed
F3  a test took a SUCCEED() escape hatch and
    proved nothing for a whole run              DEFECT, fixed
F4  shiftedFromCentroid cannot validate its
    own precondition                            LIMITATION, recorded
```

F3 is worth carrying forward as a habit, not just a fix: **a `SUCCEED()` escape hatch
in a test that sets up a hard-to-reach state is indistinguishable from a passing
test.** It was found by running the one test with `-s` and reading which assertions
fired — not by it failing, because it did not fail. If a state is worth testing, the
test asserts that it reached it.

## FULL REGRESSION

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2531/2531      0
release-ext            0        0      0          0        2531/2531      0
debug-shared-ext       0        0      0          0        2531/2531      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   12655 = 2531 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   12655 = 2531 x 5, 0 failures
qualification finished 21:48:03, 0 stage(s) failed
```

2531 = the 2479 of P15-ASSIGN-001 plus the 52 added here, and all three presets
discover the same 2531. Both repeat stages show exactly 12655 passing executions,
which is 2531 x 5 with nothing skipped.

**32903 test executions in total, 0 failures**: three full suites plus two five-fold
repeats. Qualified on the **first attempt**, 19:07:01 to 21:48:03 (2h 41m).

The stages that mattered most for this milestone, confirmed present and passing in
**all three** presets rather than only where they were developed:

```text
architecture.layering                                         passed x3
the staircase that fixes the tensor sign convention           passed x3
the configuration-override refusal                            passed x3
all 15 compile_fail.massp cases                               passed x3
```

`debug-shared-ext` was worth watching: this milestone adds a `constexpr` static-free
type to a core header and a function to `features`, and a shared build is where
dll-import linkage of header constants has bitten this repository before.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that
does not exist: 3 stages failed, exit 3. So a passing run is evidence rather than a
harness that cannot fail.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot
replace" across every log. No controlled rerun was needed.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first
build and after the last test run are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 7c6706b48b6dbcbb7677c269c2aa25d793c4747d
src bec4b1395cd6886e1f3781f5a90a6f2493f2ed3d
tests 56694e1329a9de1f648133f6223123168dfe24c7
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and
cannot affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## RESULT

```text
TASK:            P15-MASS-001
IMPLEMENTATION:  second moments of volume from the kernel (centroidal, measured
                 not assumed); kg m^2 and m^5 dimensions with four units; mass,
                 centre of mass and the inertia tensor about the centroid and the
                 origin; Huygens from the centroid; A I A^T for any rigid motion;
                 five distinct failure diagnostics; a refusal guard over the
                 carried regeneration defect
TESTS:           52 added (11 geometry, 25 features, 1 persistence, 15 compile-fail)
VALIDATION:      closed-form integrals evaluated by hand for box, cylinder, sphere,
                 fused staircase, two-solid body, drilled plate, rotation and
                 translation; four reference-free invariants; two kernel probes
                 that settled the reference point and the sign convention
RESULT:          PASS
EVIDENCE:        this directory; kernel-probe/ for the probes and their logs;
                 qualification/ for all 20 stage logs and the tree fingerprints
TODO:            updated -- 17 boxes ticked
```

## REVISION

First revision. Written against the tree qualified above; no source or test file
changed after the freeze.
