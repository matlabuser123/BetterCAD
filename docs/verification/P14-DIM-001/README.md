# P14-DIM-001 — Dimensions

```text
TASK:            P14-DIM-001
BASELINE:        2f94aa8 (P14-HLR-001)
STATUS:          PASS, with one qualification stage that failed on its first
                 run, was diagnosed, and passed on one controlled rerun.
                 See FULL REGRESSION.
```

## SCOPE

Linear, horizontal, vertical, aligned, angular, radius, diameter and ordinate
dimensions; their references, units, precision and formatting; and the
guarantee that the number comes from the model every time it is asked for.

Out of scope, and NOT done: tolerances and GD&T (P14-TOL-001), annotation and
symbol rendering (P14-ANNO-001), witness and dimension-line geometry, assembly
drawing views (P14-ASM-001). No code for any of those was written.

Carried open from the previous milestone, untouched: **P14-HLR-001's
assembly-to-assembly occlusion validation remains blocked on P14-ASM-001.**
Nothing here was broadened to close it.

## BASELINE

`2f94aa8`, clean tree, `HEAD == origin/main`. P14-HLR-001 qualified at
1847/1847 across `debug`, `release` and `debug-shared`.

## REFERENCE FEASIBILITY AUDIT

The milestone turns on one question, and it was asked before anything was
built: **what may a dimension point at?** ADR-012 already answers it — a datum
or principal plane, a datum or principal axis, a named face of a feature, a
document object, a component occurrence — and it names what is forbidden: an
`EdgeSignature`, a `FaceSignature`, a topology index, or anything whose
identity is the kernel's enumeration order.

What that vocabulary supports, checked against the code rather than assumed:

| Dimension | Needs | Expressible? |
| --- | --- | --- |
| Linear | two parallel planes, two parallel axes, or a plane and an axis parallel to it | **Yes.** `PlaneReference` already spells principal planes, datum planes AND named planar faces; `AxisReference` spells principal and datum axes |
| Horizontal / Vertical / Aligned | the same, plus the view's axes | **Yes** |
| Ordinate | a datum target and a measured target | **Yes** |
| Angular | two planes, or two axes | **Yes** |
| Radius / Diameter | a named CYLINDRICAL face | **Yes, with one addition** — see below |

What is **not** expressible, and was not faked:

1. **A dimension to a vertex.** There is no stable vertex reference, so
   corner-to-corner distances — including a rectangle's diagonal — cannot be
   expressed. The brief's suggested `diagonal = sqrt(100² + 60²)` fixture is
   therefore not implemented, and the `aligned = 50` hypotenuse fixture is
   replaced by one that measures the same 3-4-5 relationship between things
   the model CAN name (below).
2. **A dimension to an edge.** ADR-012's own answer applies: it is expressed
   between the two named faces that meet there.
3. **A radius on a HOLE feature.** Hole features name their bottom, counterbore
   floor and spotface floor (`FaceRole`), and **not their cylindrical bore**.
   So the commonest diameter dimension of all — a hole's — has no name to
   point at. A radius on an extruded or revolved circular profile does, because
   that cylinder is a `Side` face named by its profile entity, and that is what
   the radius and diameter tests use. **Closing this needs a new `FaceRole` for
   a hole's bore, which is a change to `P12`'s qualified face-naming and not
   this milestone's to make.**

Nothing was weakened to make a checkbox green: no topology index, no nearest
edge, no geometric coincidence matching.

### The one addition

`FaceInfo` reported a face's surface KIND but not a cylinder's geometry, so a
radius had nowhere to come from except a drawn curve — which the brief rightly
forbids. `FaceInfo` now carries `CylindricalFace{axis, radius}`, read straight
off the kernel's own surface, and `features::resolveFaceCylinder` resolves a
named cylindrical face the way `resolveFacePlane` already resolved a planar
one. That is an exposure of geometry the kernel already held, not a second
geometry system.

## DIMENSION CONTRACT

```text
canonical (stored)     the view; the type; the references; the display unit,
                       precision and trailing-zero policy; where the text sits
derived (never stored) the measured Length or Angle; the text
```

**The file has nowhere to put a number.** `dimensionToJson` writes no value,
and a test greps the saved document to prove it. A dimension that stored its
answer could disagree with the part it dimensions, which is the one failure a
drawing must not have.

### What the measurement is, exactly

Every linear-family dimension is one vector: the **separation**, running from
the first target to the second, perpendicular to both.

| Pair | Separation | Refused when |
| --- | --- | --- |
| plane ↔ plane | signed distance × the first plane's normal | not parallel |
| axis ↔ axis | the offset between them, perpendicular to their shared direction | not parallel |
| plane ↔ axis | signed distance × the plane's normal | the axis is not parallel to the plane |

Two things that are not parallel have no ONE distance between them: the answer
would depend on where along them it was taken, and picking a place is exactly
the silent wrong answer ADR-012 exists to prevent. The refusal names the
measurement that IS defined for them.

From that one vector:

```text
Linear      |s|                        the true model distance
Horizontal  |s . viewX|
Vertical    |s . viewY|
Aligned     hypot(s . viewX, s . viewY)   the distance AS DRAWN
Ordinate    s . viewX or s . viewY        signed, from a datum
```

**Aligned is the projected distance, not the model distance**, and the brief
asks for that to be stated rather than left accidental. The two differ exactly
when the separation has a component along the view's normal — where a
distance exists in the model and cannot be drawn.

### Angular is what a protractor reads

Between two **planes**: 180° minus the angle their normals make.
`resolveFacePlane` faces a normal out of the material, so this is the angle
measured *through* the material, which is what an angle marked on a drawing
means. It comes out right in every case: two faces of a slab read 0° and are
parallel; two coplanar faces read 180° and are flat; a wedge reads its own
included angle.

Between two **axes**: the angle between the two lines, folded into [0°, 90°],
because an axis's direction may be stored either way round and an angle that
flipped to its supplement because a datum was typed backwards would depend on
how the model was written rather than on its shape.

Computed as `atan2(|a × b|, a · b)`, never `acos(a · b)`: acos loses its
precision exactly where two faces are nearly parallel or nearly opposed —
which is where an angular dimension is most often placed — and walks out of
its domain into NaN as soon as rounding pushes the dot past 1.

### Components

A view of a component measures the part **where the solver put it** (ADR-005):
the resolved geometry is moved by the solved transform before anything is
projected. That changes nothing about a length, an angle or a radius, which a
rigid motion leaves alone, and everything about which view axis a distance
runs along.

## INDEPENDENT VALIDATION

Every fixture is a prism whose sizes are written into its own sketch, so every
expected answer is one of those numbers or a closed form on them. No expected
value is read back from the routine under test.

### Linear, from a 100 × 60 × 40 block

| Measured between | Expected | Result |
| --- | --- | --- |
| the two side faces across the width | 100 mm | exact to 1e-9 mm |
| the two side faces across the depth | 60 mm | exact |
| start cap and end cap | 40 mm | exact |
| two faces that are not parallel | refused | "not parallel", naming the angular measurement |

### The view's axes are the view's, not the world's

Front view (right +X, up +Z), same block:

| Dimension | Horizontal | Vertical | Aligned | Linear |
| --- | --- | --- | --- | --- |
| across the width | **100** | 0 | 100 | 100 |
| across the depth | **0** | **0** | **0** | **60** |

The second row is the one that matters: the block's depth lies along the view's
NORMAL, so none of it can be drawn — and the model still knows it is 60. In a
Top view the same two faces read Vertical = 60, which is the brief's
"changing Front → Top may change which axis a horizontal dimension represents",
as a number.

### Aligned splits as 3-4-5

The wedge's hypotenuse face has outward normal (40, 30, 0)/50, so a datum
plane 50 mm off it is displaced (40, 30, 0). Seen from the top:

| | Expected | Result |
| --- | --- | --- |
| Horizontal | 40 mm | to 1e-6 |
| Vertical | 30 mm | to 1e-6 |
| Aligned | 50 mm | to 1e-6 |
| Linear | 50 mm | to 1e-6 |

This is the case where all four are different known numbers and none is any
other by accident. It replaces the brief's vertex-to-vertex hypotenuse
fixture, which the reference vocabulary cannot express.

### Angular, from a 30-40-50 triangle

| Between | Expected | Result |
| --- | --- | --- |
| base and hypotenuse | atan2(40, 30) = 53.130° | to 1e-9° |
| upright and hypotenuse | atan2(30, 40) = 36.870° | to 1e-9° |
| base and upright | 90° | to 1e-9° |
| **the three together** | **180°** | to 1e-9° |
| two faces of a slab | 0° (parallel) | to 1e-9° |

The three angles summing to 180° is a check no single measurement could pass
by luck. Near-parallel planes at 0.001°, 0.01° and 179.99° all return finite
values in range, which is where `acos` would have failed.

### Radius and diameter

A rod from a circle of radius 20 extruded 80:

| | Expected | Result |
| --- | --- | --- |
| Radius | 20 mm | exact |
| Diameter | 40 mm | exact |
| diameter == 2 × radius | bit for bit | **exact equality asserted** |

Diameter is the radius doubled by the ONE path, so the two can never disagree
about one face. A radius of a planar face is refused twice over: when the
definition is built ("has no radius") and, for a cylindrical NAME on a planar
face, when it is measured ("not a cylinder").

### Ordinate

Signed, from the model's own YZ plane, in a top view: the block's right-hand
face reads +100, its left-hand face 0, and measured the other way round the
left face reads −100. A negative ordinate is information, not an error.

## THE SCALE GATE

The hard one. A 100 mm feature is 100 mm at every scale, and moving the view on
the sheet changes nothing:

| Sheet scale | Horizontal | Linear |
| --- | --- | --- |
| 1:1 | 100 mm | 100 mm |
| 1:2 | 100 mm | 100 mm |
| 2:1 | 100 mm | 100 mm |
| 1:10 | 100 mm | 100 mm |
| 5:1 | 100 mm | 100 mm |

A radius is checked the same way, and a view moved from (200, 150) to (40, 90)
measures bit-for-bit identically.

## UNITS, PRECISION AND FORMATTING

The display unit is a **symbol from the unit catalogue**, not a scale factor,
so a file says what it means. A length dimension takes a length unit and an
angular one an angle unit; mixing them is refused, because writing an angle in
millimetres is not a formatting choice but a category error that would make
the drawing read as a length.

| Value | Unit | Decimals | Text |
| --- | --- | --- | --- |
| 100 mm | mm | 1 | `100.0` |
| 100 mm | cm | 1 | `10.0` |
| 100 mm | m | 3 | `0.100` |
| 100 mm | in | 4 | `3.9370` |
| 100 mm | mm, with symbol | 1 | `100.0 mm` |
| π/2 | deg | 1 | `90.0` |
| π/2 | rad | 4 | `1.5708` |

Rounding is **half away from zero**, which is what a drawing office does.
`std::format` rounds half to even, so 0.125 at two decimals would come out
`0.12` because the digit before it is even — one dimension disagreeing with
another produced from the same number.

Getting that right needed integer arithmetic, for a reason worth recording.
A value typed in millimetres is held in **metres** (the architecture's rule
that quantities are SI inside), and 0.145 mm comes back as
`0.14499999999999999`. Worse, the double nearest 0.145 times 100 is
`14.499999999999998`, so even a value that *is* 0.145 rounds down if the last
step is another multiply. The value is therefore counted at six decimals finer
than it will be shown and then divided as integers. The guard it absorbs is at
most 1e-12 of the display unit — below anything a drawing distinguishes.

| Value | 2 decimals |
| --- | --- |
| 0.125 | `0.13` |
| 0.135 | `0.14` |
| 0.145 | `0.15` |
| 0.155 | `0.16` |
| 0.165 | `0.17` |
| −0.125 | `-0.13` |
| 0 | `0.00`, never `-0.00` |

Trailing zeros are a drawing-office choice: 100 mm at three decimals reads
`100.000` or `100`. Changing the precision is asserted **not** to change the
measured value.

## MODEL-DRIVEN VALUES

The guarantee the whole design exists for. Same `DimensionId`, same
references, no edit to the drawing:

| Extrude depth | Dimension reads |
| --- | --- |
| 40 mm | 40 mm |
| 125 mm | 125 mm |
| 7.5 mm | 7.5 mm |
| 0.25 mm | 0.25 mm |
| 999 mm | 999 mm |

And under **configurations**: a configuration overriding the depth parameter to
85 mm makes the dimension read 85 mm while its definition is asserted
unchanged; switching back reads 40 mm again.

## REFERENCES AND REBINDING

**Unrelated edits do not retarget it.** Adding a sketch, adding a datum,
renaming an object and deleting an unrelated object all leave the definition
identical and the measurement at 100 mm.

**A missing reference fails, and never shows the old number.** Deleting the
feature makes the dimension unresolved with a diagnostic naming it by name and
ID. There is no cached value to fall back to — `measure()` resolves every time
and there is nowhere for a stale number to live.

**No silent rebinding.** The hard gate. Two separate prisms each have an end
cap at exactly z = 40: the same plane, facing the same way, telling nothing
apart. A dimension is pointed at one of them and that feature is deleted. A
resolver that matched geometry would find the survivor and go on showing
40 mm as if nothing had happened. Resolution is by NAME, so the dimension
becomes unresolved — asserted, with the survivor's body asserted to still
exist so the test cannot pass by the geometry simply being gone.

## FAILURE ATOMICITY

Seven rejected edits, each checked to leave the definition byte-identical and
the measurement still 100 mm: no view, nine decimals, an unknown unit, a
radius with two targets, nothing to measure to, a target naming two things at
once, and a view that is not in the document.

## DETERMINISM

Measured eight times over, compared bit for bit on the value and exactly on
the text. Nothing in the path depends on iteration order, timing or locale:
the formatting is built from integers and `std::to_string` of integers, never
through a locale-sensitive float conversion.

## ADVERSARIAL REVIEW

The brief's nineteen questions, answered:

| Question | Answer |
| --- | --- |
| Can view scale alter the value? | No — five scales asserted, hard gate |
| Can sheet movement alter it? | No — asserted bit for bit |
| Can model and display units mix? | No — an angle in mm and a length in deg are both refused at creation |
| Can aligned become 3D distance? | No — the depth case gives Aligned 0 and Linear 60 |
| Can horizontal/vertical use world axes? | No — Front vs Top asserted |
| Can angular produce NaN near parallel? | No — atan2, and 0.001°/0.01°/179.99° asserted finite |
| Can radius come from tessellation? | No — read off the kernel's `gp_Cylinder` |
| Can diameter use a stale radius? | No — it is the radius doubled, one path, exact equality asserted |
| Can rounding differ between Debug and Release? | Integer arithmetic; the three-preset run is the evidence |
| Can changing precision change the value? | No — asserted |
| Can a missing reference leave the old number? | No — nothing is cached; asserted after deleting the feature |
| Can a dimension jump to similar geometry? | No — two identical planes, asserted unresolved |
| Can regeneration change the DimensionId? | No — it is the document object's ID |
| Can save/load change target identity? | No — definitions compared by value, and the measurement compared too |
| Can two identical holes confuse resolution? | No — that is the no-rebind test |
| Can an invalid edit partly modify the document? | No — seven refusals, each leaving the definition identical |
| Can placement become authoritative geometry? | No — placement is where the text sits and is never measured |
| Did this start tolerances / GD&T? | No |
| Did this start annotation rendering? | No |

One defect found and fixed during the review, with its regression test: the
`writeFixed` first written rounded through a final floating multiply and
produced `0.14` for 0.145 — see PRECISION above. One design smell was also
removed: the first draft carried dead code (`units` computed and discarded)
and hit exactly the `Dimension` name collision ADR-017 had warned about.

## KNOWN LIMITATIONS

Recorded, not worked around.

1. **No dimension to a vertex**, so no corner-to-corner diagonal. There is no
   stable vertex reference (ADR-012); semantic topology is P21.
2. **No radius or diameter on a HOLE feature**, because hole features do not
   name their bore. Needs a new `FaceRole` in P12's qualified face naming.
3. **Linear-family dimensions require their two targets to be parallel.**
   Non-parallel targets are refused rather than measured at an arbitrary
   place. A dimension between skew features has no expression.
4. **Two parallel datum planes read 180°, not 0°, as an angular dimension.**
   The rule is "the angle through the material", which is right for faces and
   arbitrary for datums, and one rule was preferred to a rule that changed
   with the kind of reference.
5. **A dimension whose targets lie outside its view's own source is still
   measured in that view's frame.** For a view of a component, the solved
   transform is applied to whatever the dimension resolved. Reachable only by
   dimensioning something the view does not show, and it affects only the view
   measurements; lengths, angles and radii are unchanged by a rigid motion.
6. **No witness lines, extension lines, arrows or text bounds.** The milestone
   deliberately stops at the number and its text; drawn annotation is
   P14-ANNO-001.
7. **A displayed value that is rounded is rounded for display only.** Nothing
   rounds the model.

## IMPLEMENTATION

New:

| File | Lines | What |
| --- | --- | --- |
| `include/bettercad/drawing/Dimension.hpp` | 226 | the contract: types, targets, format, the `Dimension` object, the measured result |
| `include/bettercad/drawing/Dimensions.hpp` | 84 | the document-facing operations and `measure()` |
| `src/drawing/Dimension.cpp` | 381 | validation, and the formatting that rounds in integers |
| `src/drawing/Dimensions.cpp` | 426 | resolution, the separation vector, the angle, the component transform |
| `src/io/json/DimensionJson.cpp` | 222 | persistence of intent, and of nothing measured |
| `tests/drawing/DimensionTests.cpp` | 1078 | 34 cases against arithmetic done in the test |

Changed: `Id.hpp` (`DimensionId`), `Faces.hpp` and `OcctFaces.cpp` (a
cylindrical face's exact axis and radius), `FaceReferences.hpp`/`.cpp`
(`resolveFaceCylinder`), `DocumentJson.cpp` and `ObjectJson.hpp` (the two
dispatch sites), three `CMakeLists.txt`.

## FULL REGRESSION

Three presets, each configured, fully cleaned, rebuilt with warnings as
errors, proved fresh, and only then tested.

| Preset | Build | Warnings | No-op rebuild | CTest | `Dimension_` tests found |
| --- | --- | --- | --- | --- | --- |
| `debug` | 0 | **0** | 0 compile, 0 link | **1881 / 1881** | 68 |
| `release` | 0 | **0** | 0 compile, 0 link | **1881 / 1881** | 68 |
| `debug-shared` | 0 | **0** | 0 compile, 0 link | **1881 / 1881** | 68 |

Baseline was 1847 tests; this milestone adds **34**.

### The determinism repeat, and the stage that failed

| Run | How | Result |
| --- | --- | --- |
| release repeat | harness, cp 65001 | **1880 / 1880**, `until-fail:5` |
| debug repeat, Run A | harness, cp 65001 | **FAIL** — `cli.assembly.batch`, "cannot replace built.bcad: Permission denied" |
| debug repeat, Run B | direct ctest, cp 437 | invalid as a gate (wrong code page); `cli.assembly.batch` 5/5 passed |
| debug repeat, Run C | malformed invocation | **non-result** — ctest never started |
| debug repeat, Run D | PowerShell, cp 65001 | **1880 / 1880**, `until-fail:5` |

The full account, including how Run C was proved to be a non-result, is in
`qualification/repeat-gate-attempts.md`. In short: the CLI produced no wrong
answer, it could not open a file for writing; the cause is that the build tree
lives under OneDrive, which opens files to sync them. It did not reproduce.
**Run A's failure is kept in the evidence rather than erased**, and the
structural remediation — moving build output off the synchronised directory —
is recorded as the next infrastructure decision and was deliberately not done
here, because the build directory is set in `CMakePresets.json`, inside the
frozen qualified tree.

### Two defects in the qualification tooling, found and fixed

**The harness always exited 0.** `qualify.cmd` ended `exit /b 0` whatever
happened, so a failed stage reached nobody: the qualification "passed" and the
failure sat in `qualification-times.txt` to be noticed by eye. That is how Run
A's failure was found, and it is not a gate. The harness now counts failed
stages and ends `endlocal & exit /b %FAILURES%`.

`qualification/verify-harness.cmd` is the regression, and it passes: it points
the harness at a preset that does not exist — a real failed stage, not a
simulated one — and requires a non-zero exit. It gets 3, because the configure
fails and the two repeat stages then run against a filter matching nothing,
which `ctest` also reports as a failure. That second part is worth having:
**a mistyped repeat filter cannot silently skip the determinism gate.**

**The harness file was corrupt.** It had grown 98 → 196 → 784 lines across
three milestones, because the script that copied it for each one used
`pathlib.read_text`/`write_text`, which translate newlines on both sides and
turned `\r\n` into `\r\r\n` and then into doubled blank lines. It still ran
— blank lines are inert in batch — but P14-VIEW-002's and P14-HLR-001's
committed evidence contain the mangled copies. Rebuilt here from
P14-VIEW-001's copy, the only clean one, reading and writing bytes. The two
committed copies are left alone: evidence records what ran.

`qualification/qualify-as-run.cmd` is the exact harness that ran this
qualification — corruption, `exit /b 0` and all — kept beside the corrected
one so the evidence shows what executed rather than what it should have been.

### The qualified tree is the committed tree

Git tree IDs from a scratch index, before the first build and after the last
test run, and again before the commit — identical in all three:

```text
apps             b32ce14e7be30b1c05432f740c2d25607be73b39
include          8b131c848472f53b75f18c089bd7523dc1306d83
src              41c186147a148090a55a553410a032680d77b7dc
tests            7e0d67b900926f02e41656c5a2d964dc3a1ad0b0
examples         d0d2ae4277ba99b46ff1384725292deb3519c199
cmake            a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt   a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Only `docs/` changed after the freeze, and nothing there reaches the
executable or the tests.

## RESULT

```text
TASK:            P14-DIM-001 -- Dimensions
IMPLEMENTATION:  eight dimension types; the reference vocabulary ADR-012
                 permits and nothing wider; exact cylinder geometry exposed
                 and resolved; units, precision and locale-free formatting;
                 persistence of intent only
TESTS:           34 new; 1881/1881 in debug, release and debug-shared, each
                 from clean; 1880/1880 five times over in release and in debug
VALIDATION:      100/60/40 from the block's own sketch; the view-axis split
                 including the depth that cannot be drawn; 3-4-5 as 40/30/50
                 across the view axes; atan2(40,30), atan2(30,40) and 90
                 summing to 180; radius 20 and diameter exactly twice it;
                 five sheet scales -- all computed in the tests
ADVERSARIAL:     19 questions, 1 defect found, 1 fixed, 1 regression test
WARNINGS:        0 in all three builds
DETERMINISM:     eight repeats bit for bit on value and exactly on text
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-DIM-001 -> [x]
CARRIED OPEN:    P14-HLR-001's assembly-to-assembly occlusion validation,
                 still blocked on P14-ASM-001 and untouched here
NEXT:            P14-ANNO-001 -- drawing annotations
```

## FILES

```text
qualification/qualify.cmd                 the harness, corrected
qualification/qualify-as-run.cmd          the harness that actually ran
qualification/verify-harness.cmd          its exit-code regression
qualification/run-qualification.cmd       the entry point
qualification/repeat-gate-attempts.md     the four runs of the repeat gate
qualification/qualification-times.txt     every stage, exit code, tree IDs
qualification/configure-*.log             3 presets
qualification/clean-*.log                 3 presets
qualification/build-*.log                 3 presets, 0 warnings each
qualification/rebuild-*.log               the no-op freshness proof
qualification/ctest-*.log                 3 presets, 1881/1881 each
qualification/ctest-repeat-release.log    release, until-fail:5
qualification/ctest-repeat-debug.log      Run A -- the failure
qualification/ctest-repeat-debug-second.log   Run B -- invalid, diagnostic
qualification/ctest-repeat-debug-third.log    Run D -- the valid rerun, PASS
```

## REVISION

First revision. The Debug determinism repeat was run four times; what each run
is worth, and what it is not, is in `repeat-gate-attempts.md`. No result was
discarded.
