# P12-REF-001 — Production Reference Models

```text
TASK:            P12-REF-001
SCOPE:           six realistic mechanical parts built from the complete P12
                 feature set, through the public APIs only, each validated
                 against geometry derived independently of BetterCAD
IMPLEMENTATION:  six model builders in examples/reference_models/, six saved
                 models in examples/models/reference/, one additive helper
                 (SketchBuilder::attach), and registration in the catalog so
                 the new parts inherit every cross-cutting reference-model test
TESTS:           37 new Catch2 cases in 7 files, plus 11 new CLI process tests;
                 1259 tests per preset
VALIDATION:      closed forms derived by hand for every model -- prismatoid
                 with a mixed-area term, Pappus, a drafted-box integral, the
                 offset-cavity of a drafted box, a linear variable-fillet
                 integral, Green's theorem for the pocket sections, and ISO
                 273 / ISO 724 diameters taken from the standards layer
ADVERSARIAL:     one independent review agent plus a self-review; 8 real
                 defects found and fixed, each with a regression test; see
                 "Adversarial review" below
RESULT:          see "Final result"
EVIDENCE:        this directory
```

## Scope

P12 built the capabilities one at a time: advanced holes, pattern instances,
advanced sweeps, advanced lofts, design equations and configurations, datums,
stable references, ribs, drafts, shells and variable fillets. This milestone
is the first that has to make them work **together**, in parts that look like
work rather than like test fixtures.

Six were chosen so that every P12 capability is exercised by at least one
part, and so that each part has geometry that can be checked against a closed
form derived by hand:

| Model | What it is | P12 features |
| --- | --- | --- |
| `MotorMount` | Configuration-driven mounting plate with a pilot boss | extrude, hole, linear pattern, configurations, equations, stable face reference (sketch on a named end cap) |
| `GearboxCover` | Cast cover: drafted box, hollowed, spotfaced port, tapped fixings | extrude, **datum plane**, **draft**, **shell**, hole (spotface, tapped-through), linear pattern |
| `ManifoldTube` | Square-bore tube swept down and round two bends, with a flange | extrude, **sweep** (spatial path, twist), datum plane |
| `TransitionDuct` | Square-to-round duct and a smooth nozzle on one flange | extrude, **loft** (ruled and smooth, three sections) |
| `IndexPlate` | Lightened index plate, elliptical pockets, hub | extrude, **circular pattern**, **combine**, datum axis |
| `RibbedBracket` | Angle bracket with a gusset and a variable-radius corner | extrude, **rib**, **variable fillet**, hole (ISO 273 clearance), coordinate system, datum plane |

Between them the six use every feature kind the reference suite requires. The
`ReferenceModel_FeatureCoverage` test enforces this and now demands five more
kinds than it did before this milestone (`shell`, `draft`, `rib`,
`variable_fillet`, `combine`) — the coverage list was **tightened**, not
relaxed, and the matrix is printed into the test log.

## Architecture

Nothing in this milestone changes the architecture. The models are built
entirely through the public document, parameter, sketch and feature APIs —
the same path a user or a script takes. No geometry is created directly, no
private header is included, and no test-only entry point exists.

One helper was added, and it is purely additive:

```cpp
// examples/reference_models/BuildSupport.hpp
void SketchBuilder::attach(const PlaneReference& reference);
```

which forwards to the existing `Sketch::setAttachment`. It has no other
callers and changes no existing behaviour.

## Blast radius

The builders and their tests are leaves: nothing in `src/` or `include/`
depends on them. The three shared things they do touch:

- `ReferenceModels.hpp` / `Catalog.cpp` — new rows in `kReferenceModels`.
  This is the point of leverage: adding a row enrols a model in every
  cross-cutting test the suite already has (build, validate, stress,
  regenerate, saved-file comparison, feature coverage, change-and-restore).
  Six rows therefore added six models to ten existing tests.
- `AllModelsTests.cpp` — the coverage list, tightened as above.
- `tests/CMakeLists.txt` — new test files and CLI process tests.

Every deleted line in the whole milestone diff was checked: four in total, all
of them a moved closing parenthesis, an extended list, or a comment that had
to be updated. **No test was deleted, disabled, skipped, renamed or weakened,
and no existing golden value was edited.**

## Independent validation

No expected value in this milestone is read back from BetterCAD. Each is
derived from the model's definition by hand and written into the test beside
the derivation. The main ones:

**Prismatoid with a mixed-area term** (`TransitionDuct`, ruled half). Between
matched sections the cross-section is quadratic in height, so

```text
V = h/6 (A0 + 4 Am + A1),   Am = (A0 + M + A1)/4
```

is exact, with `M` the mixed area. For a regular n-gon of circumradius `R`
against a circle of radius `r`, `M = (2 n^2 r R / pi) sin^2(pi/n)`; for a
square that is `16 r R / pi`. Measured to `1e-10`.

**Pappus** (`ManifoldTube`). A section whose centroid rides the path sweeps
`area x path length`. Checked at three section sizes and four twist angles.

**Drafted-box integral** (`GearboxCover`). A box drafted by `a` on four sides
about `z = 0`:

```text
V = LWH - t(L+W)H^2 + (4/3)t^2 H^3,   t = tan(a)
```

**Offset cavity of a drafted box** (`GearboxCover`, added by this review).
Each side is offset inward along its own normal, which for a face tilted by
`a` moves it `t/cos(a)` horizontally, and the flat top moves straight down by
`t`. So the cavity is *another drafted box*:

```text
cavity = draftedBox(L - 2t/cos a, W - 2t/cos a, H - t, a)
```

and the shell leaves `draftedBox(L,W,H,a) - cavity`. The shell is the
dominant feature of that part; before this review it had no numeric
expectation at all.

**Linear variable-fillet integral** (`RibbedBracket`, added by this review).
A fillet between faces at right angles removes `r^2 (1 - pi/4)` per unit
length, so a radius running linearly from `r0` to `r1` over length `L`
removes

```text
V = (1 - pi/4) L (r0^2 + r0 r1 + r1^2) / 3
```

This is what separates a *variable* fillet from a constant one: at 2 → 4 mm
it is `9.33 L (1-pi/4)`, against `16 L (1-pi/4)` for a constant 4 mm fillet.

**Standards, not numbers.** Thread minor diameters and clearance-hole
diameters come from the standards layer (ISO 724, ISO 273), never from a
number typed into a test.

## Adversarial review

The review was run two ways: an independent agent given the diff and asked to
disprove the milestone, and a self-review against the checklist in
`docs/engineering/ADVERSARIAL_REVIEW.md`. It found **eight real defects**.
All eight are fixed, and each fix carries a regression test that fails
without it. This section records them because the defects are more
informative than the feature list.

### 1. `GearboxCover` — `height` broke the model outright

The inspection port and the fixing holes were placed on a `FaceSignature`
written as the literal plane `z = 40` — the outside face, but only at the
height the model happened to be authored at. Reproduced:

```text
object:13: InspectionPort: hole: the placement face (plane through (0, 0, 40)
mm facing (0, 0, 1)) matches no face of the body
object:14: blocked
object:15: blocked
```

BetterCAD is right here: a `FaceSignature` is documented geometric matching,
and a plane that has moved matches no face and **says so** rather than
silently drilling a different one. The model was at fault. No test drove
`height`, so nothing noticed.

**Fix.** The cover now hangs *below* its outside face: `z = 0` is the
outside for every height, and the faces that do move are reached by
references that follow — the parting face by a datum driven by `-height`, the
shell's open face by a *named* face of the extrude. **Regression:**
`ReferenceModel_GearboxCoverFollowsItsHeight` drives the height up and down
and checks the closed form at each.

### 2. Frozen centring constraints in two models

`ManifoldTube`'s bore section was centred on the path by two constraints
written as the literal `6.0` — half the side it was authored at. The comment
directly above them claimed "centred on the origin, so the section's centroid
rides the path and Pappus gives the volume exactly". At any other `side` the
centroid slid off the path and **the model quietly lost the property its own
validation rests on**. The same defect appeared twice more: the manifold's
flange (`20.0`) and the duct's inlet square (`40.0`), the latter being what
makes the loft concentric and the prismatoid formula applicable.

**Fix.** All four are now driven by `half_side = side / 2` equations.
**Regressions:** `ManifoldTubeFollowsItsSection` and
`TransitionDuctFollowsItsInlet` drive the sections and re-check Pappus and
the prismatoid at the new sizes.

### 3. The variable fillet was tested by a bound a constant fillet would pass

The test bounded the removed material between constant fillets at the two
station radii. Because the cross-section goes as `r^2`, that envelope spans a
factor of four — and a **constant 4 mm fillet sits inside it**. The one
property the feature exists to provide was not tested. The bound also used
the wrong edge length: it assumed the corner ran the full 65 mm height, but
the base fills the corner up to `z = thickness`, so the convex edge is
`wall_h - thickness = 55` mm long.

**Fix.** The closed form above, at the real 55 mm. Measured **111.135 mm³**
against **110.162 mm³** predicted — 8.8e-3, which is the blend's two ends
running out into the adjoining faces, material the prismatic integral does
not model. Bounded at 1.2e-2, which still separates the linear law from a
constant 4 mm fillet (188.9 mm³) by a factor of 1.7.

### 4. A tolerance of mine that could not be justified

`P12ModelsTests.cpp` introduced `kRelRestored = 1e-6` for change-a-parameter-
and-restore, justified by reasoning about a twisted sweep's fit error. The
review found that `AllModelsTests.cpp` **already** performs the identical
operation over the same models with the same parameters at the same values,
and requires `1e-12` / `1e-9`. Both could not be true.

The pre-existing bound was the correct one: the fit is deterministic and
reproduces exactly when the parameter returns. The constants are now
`1e-12` / `1e-9` and pass. A weaker bound would have quietly exempted the new
models from a gate the older ones already clear.

### 5. `MountFrame` was a datum that drove nothing

`RibbedBracket` created a coordinate system and documented it as the datum
"so the whole bracket can be moved by moving one datum". It was never
referenced again: both sketches were drawn on bare principal frames and the
rib on a frame frozen at `x = 45`.

**Fix.** The two sketches and the rib's datum now hang off the frame.
**Regression:** `RibbedBracketIsBuiltOnItsFrame` moves the frame and checks
the solid geometry follows it rigidly — and records, as an executable fact,
exactly how far that goes (see Known limitations).

### 6. `RibbedBracket` — `width` was pinned twice

The gusset's sketch plane was a frame frozen at `x = 45` (`width/2`) and the
filleted edge a line signature at `x = 90` (`width`). Widening the bracket
left the rib off-centre and the fillet's edge matching nothing.

**Fix.** The rib sits on a datum driven by `width/2`, and the filleted corner
is the one at `x = 0`, which no parameter moves. **Regression:**
`RibbedBracketFollowsItsWidth` at 70 and 120 mm.

### 7. `MotorMount` was only half parametric

The bolt holes were driven *across* the plate but frozen *up* it at a literal
30 mm, and the pilot boss's centre was the literal `(45, 22.5)`. The plate is
`width x width/2`, so those were the middle of the 90 mm plate only: in
`Medium` and `Large` both features sat off-centre. **No volume check could
ever have caught this**, because moving a hole or a boss does not change how
much material it removes or adds.

**Fix.** Both are driven by `half_width` / `half_height` equations.
**Regression:** `MotorMountStaysCentredInEveryConfiguration` measures the
part's **centroid** in all three configurations, not its volume.

### 8. `MotorMount` had no failure-path coverage

Every other new model had an atomic-failure-and-recovery test; this one had
none.

Writing it exposed something worth recording: **the model is scale-invariant**.
Every dimension is an equation on `width`, so the hole edges land at `w/10`
and `0.9w` whatever `w` is, and no width, however small, ever puts a hole off
the plate. The first attempt at this test assumed the opposite and passed a
regeneration it expected to fail. The failure has to come from breaking a
relation, not from scaling one. The test now frees the bolt inset from its
equation and drives it past the edge of the plate:

```text
object:11: BoltHole: hole: the centre (200, 30) mm is not on a face of the
body on the plane through (0, 0, 0) mm facing (0, 0, -1)
(1 face(s) lie on that plane elsewhere)
```

and checks the failure is atomic (no downstream body, document object count
unchanged) and that restoring the equation restores the model exactly.

### Also examined, and found sound

- **No expected value is generated by BetterCAD.** Every `WithinRel` target
  in the seven new files was checked mechanically. The single hit in the
  whole reference suite is a *symmetry* assertion in the pre-existing
  `BearingHousingTests.cpp` (a mirrored body is twice its half), whose
  absolute value is separately pinned to a closed form at `1e-14`.
- **No parameter drives nothing.** All 48 parameters across the six models
  were traced to a feature field or to an expression that reaches one.
  `nozzle_h` reaches geometry through a two-level chain
  (`duct_h -> nozzle_h -> mid_offset/outlet_offset -> loft section offsets`).
- **No kernel index, handle or address is persisted** as engineering intent.
  Every reference is a semantic name (`FaceRole::EndCap`), a geometric
  signature, a datum object, or a principal plane.

## Failure paths

Each model has an atomic-failure-and-recovery test: a parameter driven to
something the geometry cannot satisfy must fail, name what failed and why,
leave no downstream body, leave the document otherwise untouched, and recover
exactly when the parameter is restored.

| Model | Failure exercised |
| --- | --- |
| `MotorMount` | bolt inset driven off the plate |
| `GearboxCover` | wall thicker than the part can hold |
| `ManifoldTube` | a path the kernel cannot sweep |
| `TransitionDuct` | a throat of zero radius |
| `IndexPlate` | pockets that would meet |
| `RibbedBracket` | a fillet that does not fit |

## Persistence, determinism, undo

`P12ModelsTests.cpp` holds the contracts that must hold for all six at once:

- **Save/load** runs the real round trip — build, save, destroy, load,
  regenerate, compare — and compares engineering intent (IDs, names, feature
  count, parameters, expressions, configurations) as well as topology,
  volume, area, centroid and bounds, all exactly.
- **Determinism** regenerates twice and builds a second document from
  scratch; both must give the same fingerprint exactly.
- **Built together vs. alone** catches hidden global state.
- **Undo/redo** drives the same edit through `CommandHistory`.
- **STEP** export and read-back checks the kernel's own reader finds one
  valid solid of the same volume and bounds. Nothing claims the feature tree
  or parameters survive STEP, because they do not.

`examples/models/reference/*.bcad` are the six documents as their builders
make them, and `ReferenceModel_SavedModelsMatchTheBuilders` requires saving a
freshly built document to reproduce the committed file **byte for byte**.

## Known limitations

Stated because they are real, not worked around:

- **A model placed by geometric references cannot be relocated by moving its
  datum.** `RibbedBracketIsBuiltOnItsFrame` records this precisely: moving
  `MountFrame` moves the two plates and the rib rigidly, because those hang
  off the frame, but the bolt hole (a plane signature) and the corner fillet
  (an edge signature) name planes and edges in *model* space and therefore
  fail — loudly and atomically, which is the documented contract. Making them
  follow needs semantic face naming for holes and fillets, which P12 does not
  have.
- **`HoleDefinition.face` takes only a `FaceSignature`**, not a `FaceName`,
  unlike shell, draft and sketch attachment. This is why `GearboxCover` had
  to be re-datumed rather than simply naming its outside face.
- **The smooth nozzle's shipped configuration has no exact closed form.**
  With the ends equal the quadratic of revolution is exact to `1e-10`; with
  them unequal — which is what the model ships — it is 1.0e-3 away. The
  analytic claim for that half is therefore the *envelope* (below the ruled
  loft of the same sections, above half of it), which follows from the
  sections alone. The 1.1e-3 figure in the test is a **regression guard sized
  by measurement, not an analytic tolerance**, and is labelled as such in the
  test.
- **CLI volume goldens are regression guards.** They are BetterCAD outputs by
  construction, cross-checked against the closed forms where those exist.
- Analytic **area and centroid** checks are thinner than the P11 models':
  centroid is validated analytically for `MotorMount` (position) and
  `RibbedBracket` (rigid motion), and area and centroid are compared exactly
  across save/load, determinism and restore, but most per-stage analytic
  checks pin volume and bounds only.

## Qualification

Run by `qualification/run-qualification.cmd` on the final tree, after every
review-driven fix was in. An earlier run was **deliberately abandoned**: a
test changed while it was in flight, which voids a qualification under the
rule this repository now states explicitly ("if source or tests change after
the freeze, the qualification is void and is run again from clean"). Its
partial log was overwritten by this run.

Each preset: configure, **clean**, full build, full `ctest`.

| Preset | Build | Tests | Warnings |
| --- | --- | --- | --- |
| `debug` | 407 targets, exit 0 | **100% passed, 1259/1259** | 0 |
| `release` | 407 targets, exit 0 | **100% passed, 1259/1259** | 0 |
| `debug-shared` | 407 targets, exit 0 | **100% passed, 1259/1259** | 0 |

Determinism repeats (the milestone's selection re-run to catch order- and
timing-dependence):

| Repeat | Result |
| --- | --- |
| `release` | **100% passed, 1063/1063** |
| `debug` | **100% passed, 1063/1063** |

Timings are in `qualification/qualification-times.txt`; the raw logs are
beside it. Start 03:40:54, finish 04:38:02.

**Cross-preset determinism.** `qualification/collect-evidence.cmd` runs the
whole reference suite under each preset with `--reporter xml --rng-seed 1`
and `values.py` extracts every passed assertion that shows a value, with its
test, section and `INFO` context. The three files were then compared by
`compare-values.py`:

```text
release:      8695 non-empty lines after normalizing, MD5 4fe78930314c21eb9bb7808aee54620f
debug:        8695 non-empty lines after normalizing, MD5 4fe78930314c21eb9bb7808aee54620f
debug-shared: 8695 non-empty lines after normalizing, MD5 4fe78930314c21eb9bb7808aee54620f

debug against release:        0 expected only, 0 measured
debug-shared against release: 0 expected only, 0 measured

RESULT: measured values identical
```

Every measured geometric quantity — volume, area, centroid, bounds, every
derived parameter — is **bit-identical** across optimization levels and
across static and shared linking. The release file is kept as
`qualification/reference-values-release.txt`; the comparison as
`qualification/values-comparison.txt`.

Spot-checking that file also shows the closed forms are not fitted to the
output. The drafted-box cavity derived in this review lands at

```text
shell expected 89557.6 mm^3, actual 89557.6, rel error 1.62487e-16
```

which is machine precision, from a formula written before the number was
seen.

**Qualified tree.** `qualify.cmd` records the Git tree IDs of the built
sources from a scratch index. These were recomputed after the run and match
exactly, so the tree that was qualified is the tree that is committed:

```text
apps              9b7b03a72ef1a773304a38dd02690818f66d50a3
include           0347b09b01baf7ad55ec6a67d3a8e3256676e3a1
src               b5f9b6e92cc4ee1451f67fda6983f89c4b222435
tests             b2184a7bdb512f1882bbc84dce0449727b18ff98
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`src/` and `include/` are **unchanged from `a9edc4c`**, the commit before this
milestone: P12-REF-001 changed no library code. Everything it adds is under
`examples/` and `tests/`.

Environment: GCC 16.1.0 (MinGW-W64 ucrt), CMake 4.4.2, Ninja 1.13.2, OCCT
8.0.1, Catch2 3.16.0, Windows 11, AMD Ryzen 7 5800H.

## Final result

```text
RESULT: PASS

All reference models valid          PASS  12 models build, validate with no
                                          issue, and give one sound solid
independent validation              PASS  closed forms derived by hand for
                                          every model; no expected value is
                                          read back from BetterCAD
persistence                         PASS  real round trip per model; saved
                                          .bcad reproduced byte for byte
determinism                         PASS  repeat regeneration, rebuild from
                                          scratch, built-together-vs-alone,
                                          and 2 x 1063 repeat runs
stable references                   PASS  named faces, driven datums and
                                          geometric signatures, each used
                                          where it holds; the boundary
                                          between them is tested, not assumed
CLI/STEP                            PASS  11 new process tests through the
                                          real executable; STEP read-back
full regression                     PASS  1259/1259 on three presets from
                                          clean
0 unexpected warnings               PASS  0 in all three builds
```

The adversarial review is the reason this section can be written. Eight real
defects were found **after** the implementation was complete and its first
test run was green — four of them parameters that destroyed their own model,
one a tolerance of mine that could not be justified, one a datum that drove
nothing, one a test that could not distinguish the feature it was testing
from a simpler one, and one missing failure path. None would have been caught
by running the tests that existed.

## Revision

Qualified on the tree committed as this milestone; the eight tree IDs are
listed above. Decision recorded as
[ADR-001](../../architecture/decisions/ADR-001-reference-model-datum-placement.md).
