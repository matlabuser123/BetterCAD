# P12-SKETCH-003 — Sketches on Arbitrary Planar Faces Verification

## Status

**PASS.** A sketch or datum can be placed on any named planar face:

- the caps and flat sides of extrudes, revolves and sweeps (a sweep's sides
  also by the path edge they run along);
- the end caps of lofts;
- the bottoms and counterbore floors of holes;
- the face each edge reference of a chamfer cuts;
- the copies of any of these that linear and circular patterns and mirrors
  make.

Each reference is resolved by name in the body of the feature that made or
last copied the face, with no geometric fallback. It follows the face when
the parameters that drive it change. It fails with a structured diagnostic
when the face is not a plane or no longer exists, and it never takes another
face or another instance instead.

This milestone first stopped on 2026-09-17, because the reference
architecture could not keep a face across rebuilds (see *History*). The
project owner then authorized `P12-STREF-001` (stable feature face
references) as its prerequisite. That milestone passed at `8dbb722`, and
this one extends its names from extrudes to the other features.

Debug, Release and Debug-shared each passed **919/919** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-STREF-001` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit (see *Legacy
regression*).

Date: 2026-09-17. `main` was at `8dbb722` (`P12-STREF-001`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-SKETCH-003`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Revolves name their caps (partial turns) and the side each profile entity sweeps | IMPLEMENTED | `FaceNames_RevolutionsNameTheirCapsAndSides`; `SketchOnFace_RevolveCapsAndSidesFollowTheTurnAndLength`; `SketchOnFace_RevolveStartCapStaysOnTheSketchPlaneInEveryDirection` |
| Sweeps name their caps (open paths) and the side each profile entity sweeps along each path edge | IMPLEMENTED | `FaceNames_SweepsNameTheirSidesAlongEachPathSegment`; `SketchOnFace_SweepCapsAndSidesFollowTheirPathEdges` |
| Lofts name their end caps | IMPLEMENTED | `FaceNames_LoftsNameTheirEndsOnly`; `SketchOnFace_LoftCapsFollowTheSectionOffsets` |
| Holes name a blind hole's bottom and a counterbore's floor | IMPLEMENTED | `FaceNames_HolesAndChamfersNameTheirFacesAndKeepTheirInputs`; `SketchOnFace_HoleBottomsAndCounterboreFloorsFollowTheirDepths` |
| Chamfers name the face each edge reference cuts | IMPLEMENTED | `FaceNames_HolesAndChamfersNameTheirFacesAndKeepTheirInputs`; `SketchOnFace_ChamferFacesFollowTheDistance` |
| Patterns and mirrors name their copies; transforms, fillets and chamfers carry names | IMPLEMENTED | `SketchOnFace_PatternAndMirrorCopiesFollowTheirInstance`; `SketchOnFace_CircularCopiesFollowTheCount`; `FaceNames_PrismNamesItsCapsAndSides` (moved copies, renaming); `FaceNames_HolesAndChamfersNameTheirFacesAndKeepTheirInputs` (chamfer, fillet) |
| Sketches and datums on any named planar face; references to copies depend on the copying features; failures | IMPLEMENTED | `SketchOnFace_PatternAndMirrorCopiesFollowTheirInstance` (datum on a copy); `SketchOnFace_ReferencesToCopiesDependOnTheCopyingFeatures`; `SketchOnFace_CurvedAndRemovedFacesFailWithoutSubstitution`; `SketchOnFace_RolesAndCopiesAFeatureDoesNotHaveAreRefused` |
| Validation, CLI description and save/load of the new roles and copies | IMPLEMENTED | `SketchOnFace_ValidationReportsRolesAndCopiesTheFeatureDoesNotHave`; `FaceReferenceCli_InfoDescribesFacesOfEveryKindAndCopies`; `FaceReferenceCli_ValidateReportsCopiesAndRolesAFeatureDoesNotHave`; `cli.info.post-row`, `cli.validate.post-row`, `cli.export-step.post-row`; `FaceKindFile_*`; `SketchOnFace_RegeneratesDeterministically` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- names for the faces a fillet makes, or a loft's ruled sides (B-splines,
  never planes);
- names that survive later features (a reference is still resolved in the
  body of the feature that made or copied the face);
- edge names;
- face references in mirror planes, holes, chamfers and fillets, which keep
  their geometric signatures (P11).

This is not the semantic-topology phase.

## Design

```text
FaceRole                              + hole_bottom, counterbore_floor, chamfer
FaceSelector                          + along (a sweep's path edge), edge (a chamfer's edge
  (core, document/References)         reference, from 1), copies [{feature, instance}]
referencedObjects()                   a reference's object and copying features (dependencies)
SweptFace, SweptFaceNamer             the namer of every swept solid (was PrismFace, PrismFaceNamer)
makeRevolution/makeSweep/makeLoft     name their caps and sides through the kernel's history
cutHole(..., HoleFaceNamer)           names the cutter's bottom and counterbore step
chamferEdges(..., ChamferFaceNamer)   names the faces each reference's edge generates
buildBlend (chamfers, fillets)        carries the input body's names (Modified/IsDeleted)
transformed/translated                the moved copy keeps its names
renameFaces()                         maps a body's names (same shape)
sweptFaceNamer, hole/chamferFaceNamer the features' namers; appendCopy() for copies
buildPattern(..., pattern)            each instance's faces named with {pattern, instance}
MirrorRegeneration                    a body mirror's image renamed with {mirror, 1}
FaceReferences                        kinds, roles, copies; resolution in holderOf()'s body
Validation, DatumJson, CLI            copies checked; "along", "edge", "copies"; descriptions
```

**Names.** A face name is the generating feature and a selector:

| Role | Selector | Generated by |
| --- | --- | --- |
| `start_cap`, `end_cap` | — | extrudes, revolves (partial turns), sweeps (open paths), lofts |
| `side` | `entity` | extrudes, revolves |
| `side` | `entity`, `along` | sweeps |
| `hole_bottom` | — | blind holes |
| `counterbore_floor` | — | counterbored holes |
| `chamfer` | `edge` (from 1) | chamfers |

A copy appends `{feature, instance}` to `copies`. Patterns number their
instances from 1 (instance 0 is the original, which is not a copy). A
mirror's image is instance 1. Copies of copies append in order: the face of
`Post` in copy 2 of `Row`, mirrored by `Flip`, is
`{Post, side from line 6, copies [{Row, 2}, {Flip, 1}]}`. The start cap is on
the sketch plane (behind it when symmetric): for a negative revolve, as for a
reversed extrude, the kernel's first and last faces swap roles.

**Generation** (`kernel-probe/generation-history-probe.log`, OCCT 8.0.1):

| Operation | Probe | Used |
| --- | --- | --- |
| revolution, 90° | `FirstShape`/`LastShape` are the caps; `Generated(edge)` and the sweep's `Shape(edge)` give each side | caps from `FirstShape`/`LastShape`, sides from `Shape(edge)` |
| revolution, 360° | the first and last shapes are the profile, not faces of the result; `Generated(edge)` gives nothing for the two flat sides; `Shape(edge)` gives all four | the same (the probe is why `Shape(edge)` is used) |
| pipe shell, line–arc–line | `Generated(profile edge)` gives three faces in path order; the caps are faces | caps; a side per path segment, only when the count matches |
| pipe shell, closed path | `FirstShape`/`LastShape` are wires; each edge generates one face | no caps |
| ruled loft | `FirstShape`/`LastShape` are the end faces; the sides are B-splines | caps only |
| counterbored cutter | section edges 2 and 4 sweep the step and the bottom; in the drilled block those faces are kept | the cutter's names carried by the boolean |
| chamfer, fillet | `Generated(edge)` is the new face; the input faces are modified or kept | chamfer faces named; input names carried |
| transform (translation, reflection) | each input face is modified into one face | names carried |

**Copies.** A pattern of an extrude or revolve moves the named tool and
renames it; a pattern of a hole or chamfer names the moved instance's faces
directly. A body mirror renames the mirrored body. The booleans that combine
an instance with the body carry all the names. A reference to a copy is
resolved in the body of the last copying feature (`holderOf()`), and depends
on every copying feature.

**Resolution** follows `P12-STREF-001` and adds checks. `checkFaceName()`
refuses:

- a role the feature does not generate (a revolve's path edge, a sweep's
  side without one, a loft's side, a through hole's bottom, a simple hole's
  floor);
- an edge reference past the chamfer's list, or a path edge not in the
  sweep's path;
- a pattern or mirror named as the generator, and a fillet;
- a copy by a feature that makes none, and a mirror copy other than 1.

A copy the pattern no longer makes is not a face of the body: NotFound.

**The reference models** (`tests/support/FaceKindModels.hpp`; the saved
post row is `examples/models/post_row.bcad`):

| Model | Driving | Sketches on |
| --- | --- | --- |
| RevolvedRing | `turn` 120°, `ring_length` 20 mm | the end and start caps of a symmetric partial ring; the flat side of its top line |
| SweptBar | `lift` (a datum plane), `rise`, `run`, `thick` | the start and end caps; the right line's side along Up and along Across; the top line's side along Bend |
| LoftedFrustum | `base_offset`, `top_offset` | both caps of a square frustum |
| DrilledBlock | `bore_depth`; Seat's counterbore depth (an edit) | Bore's bottom; Seat's floor and bottom |
| BevelledBlock | `bevel` | the faces of edge references 1 and 2 |
| PostRow | `pitch`, `posts` | the right side of Row's copy 2 and its image in Flip; a datum 5 mm off copy 1's right side |
| SpokeHub | `spokes` | the upper side of the circular pattern's copy 1 |

Every sketch carries a circle, and every feature on it is a new-body
cylinder, so each is checked on its own.

## Independent Validation

Expected values are written out in `FaceKindModels.hpp` from the parameters
and from the frame rule of `faceFrame()`. The rule: the origin is the plane's
point nearest the model origin, X is a projected model axis, and
Y = normal × X.

- Frames are derived by hand for tilted faces: revolve caps at ±turn/2,
  chamfer faces at 45°, and a circular copy's side at 360°/n.
- Cylinders give π r² l; their centres are the base plus half the length
  along the normal. Their bounds reach r √(1 − aᵢ²) past the end circles
  along each axis i.
- The ring is (θ/2)(R² − r²)L, its centre the annular sector's centroid.
- The bar sums Up, the bend and Across (Pappus). The bend's centroid is the
  annular sector's, on the diagonal.
- The frustum is h/3 (A₁ + A₂ + √(A₁A₂)).
- The drilled block and bevelled block subtract cylinders and triangular
  prisms.
- The post row adds 2000 mm³ per post, its mirror doubles it.
- The hub adds 1000 mm³ per lug.

The table comes from `deviations.txt` (`deviations.py` over
`reference-values-release.txt`), which also lists the largest cases:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Sketch frames on every kind of face: origins | 1e-12 mm | 226 | 1.4e-14 mm |
| Sketch frames: axes and normals | 1e-14 | 481 | 1.1e-16 |
| Centres of mass (the seven models' bodies and all bosses) | 1e-9 mm | 303 | 1.2e-10 mm, the revolved ring's; every other body within 3.2e-13 mm |
| Volumes (bodies and bosses) | 1e-12 rel | 98 | 1.1e-15 |
| Bounds, every body but the loft (tilted cylinders included) | 1e-7 mm | 554 | 1.4e-14 mm |
| Loft bounds: the exact box inside the kernel's, at most 1e-7 mm of padding beyond (+1e-9 mm) | — | 24 | the box exceeds the exact one by 1.0e-7 mm on every side, the padding |
| Named faces' normals (geometry tests) | 1e-12 | 65 | 1.1e-16 |
| Named faces' plane points (geometry tests) | 1e-9 mm | 73 | 3.6e-15 mm |
| Named faces' areas (sectors, chamfers, floors, trimmed tops) | 1e-12 rel | 20 | 1.6e-15 |
| Moved copies' faces | 1e-9 mm | 2 | 0 |
| STEP read-back volume and bound of the post row | 1e-9 rel | 2 | 0 |

**Driven geometry.**

| Test | Changes | What follows |
| --- | --- | --- |
| Revolve | `turn` 120° → 150° | both caps' frames turn from ±60° to ±75° |
| Revolve | `ring_length` 20 → 30 mm | the top face rises |
| Sweep | `lift` 10 → 15 | the datum plane, the path's start and the start cap rise |
| Sweep | `rise` 40 → 50 | the bend and Across rise |
| Sweep | `run` 50 → 60 | the end cap moves out |
| Sweep | `thick` 8 → 12 | the top faces rise |
| Loft | `base_offset` 5 → 8, `top_offset` 35 → 45 | both caps move |
| Hole | `bore_depth` 12 → 18 | Bore's bottom sinks |
| Hole | Seat's counterbore depth 5 → 8 (an edit: the depth has no parameter) | the floor sinks, and Seat's bottom stays at z = 10 |
| Chamfer | `bevel` 4 → 6 | both chamfer planes move |
| Pattern | `pitch` 30 → 35 | copy 2's side (80 → 90), its image (120 → 110) and the datum (55 → 60) move |
| Pattern | `spokes` 4 → 3 | copy 1 turns from 90° to 120° |

At every step all frames, cylinder volumes, centres and bounds, and the
feature bodies, match the analytic values. The revolve test also checks the
positive and negative directions.

**Undo.** Undo returns every model to its first values. Where no sketch was
re-solved (turn, the hole, chamfer, loft and pattern tests), every placement
and volume is restored bit for bit.

Where a parameter drives a sketch dimension (`ring_length`, and all four
sweep parameters), undo re-solves the sketch from its changed shape. The
solver returns to the old shape only to rounding: a probe during development
found 0.019999999999999997 m where 0.02 m was. Those tests check the
analytic values after undo, not bits. This is a property of the sketch
solver, not of face references.

**Wrong-face and wrong-instance prevention.**

- *Pattern count.* With `posts` 3 → 2, copy 2 is gone. The sketch on it, and
  on its image, fail with NotFound. Copy 1's side, still in the body at
  x = 55, is not taken, and the failed sketches keep their last placement.
- *Circular count.* With `spokes` → 1 the sketch on copy 1 fails.
- *Copy order.* Copies named in the wrong order are not found.
- *Hole bottoms.* Seat's body holds two hole bottoms, one per feature
  (z = 18 and 10), and one counterbore floor, Seat's. Each resolves to its
  own, and Bore's floor does not exist.
- *Sweep sides.* The same profile line gives a plane along Up, a cylinder
  along Bend and another plane along Across. Each name resolves to its own
  face, and the cylinder is refused.
- *Chamfer faces.* Edge references 1 and 2 give faces facing (0, -s, s) and
  (0, s, s), and each sketch lands on its own.

**Removed faces.** At `turn` 360° the ring has no caps. The two sketches on
them fail with NotFound, blocking their bosses, while the sketch on the top
side, a full annulus, still resolves.

## Diagnostics

Asserted verbatim by the tests (sketch messages carry the sketch's label in
front, as in `P12-STREF-001`):

| Case | Code | Message |
| --- | --- | --- |
| a revolve side with a path edge | InvalidArgument | `Ring (object:4) is a revolve, whose sides are not named by a path edge` |
| a role the kind lacks | InvalidArgument | `Ring (object:4) is a revolve, which has no hole bottom`; `Bar (object:8) is a sweep, which has no chamfer face`; `Frustum (object:5) is a loft, which has no counterbore floor`; `Bore (object:4) is a hole, which has no end cap`; `Bevel (object:4) is a chamfer, which has no end cap` |
| a sweep side without a path edge | InvalidArgument | `Bar (object:8) is a sweep, whose sides are named by a profile entity and a path edge` |
| a path edge not in the path | NotFound | `Bar (object:8): entity:99 is not an edge of its path` |
| a loft side | InvalidArgument | `Frustum (object:5) is a loft, whose sides are not planes and are not named` |
| a simple hole's floor | InvalidArgument | `Bore (object:4) is not counterbored, so it has no counterbore floor` |
| a through hole's bottom | InvalidArgument | `Bore (object:4) is a through hole, which has no bottom` |
| an edge reference past the list | NotFound | `Bevel (object:4) has 2 edge references, not 3` |
| a fillet | InvalidArgument | `Round (object:9) is a fillet, whose faces are not named (extrudes, revolves, sweeps, lofts, holes and chamfers name theirs)` |
| a pattern or mirror as the generator | InvalidArgument | `Row (object:7) is a linear pattern, whose faces are copies: name the face it copies, and the copy`; the same for `Flip (object:8) is a mirror` |
| a copy by an extrude | InvalidArgument | `Plate (object:4) is an extrude, which makes no copies (patterns and mirrors do)` |
| a mirror instance other than 1 | InvalidArgument | `Flip (object:8) is a mirror, whose only copy is instance 1, not 2` |
| a copy not made | NotFound | `the side from entity:6 of Post (object:6), copy 9 of Row (object:7) is not a face of its body (the feature's operation left no such face)` (also after `posts` 3 → 2, and for `spokes` → 1) |
| a cylinder | InvalidArgument | `the side from entity:N of Ring (object:4) is a cylinder, not a plane`; `the side from entity:R along entity:B of Bar (object:8) is a cylinder, not a plane` |
| a full turn's caps | NotFound | `the end cap of Ring (object:4) is not a face of its body (…)` |
| a copying feature deleted | NotFound | `object:11 references object:8, which does not exist` (regeneration); `object:8 does not exist` (resolution) |
| instance 0, feature 0 | InvalidArgument | `a sketch's attachment: a copy is an instance from 1 (instance 0 is the original)`; `… a copy must name a valid feature` |
| validation | error | `CopySketch (object:9): the attachment is the side from entity:6 of Post (object:6), copy 2 of Plate (object:4): Plate (object:4) is an extrude, which makes no copies (patterns and mirrors do)`; a missing copying feature is reported once, as a missing reference |
| file | — | `….copies[0].instance: expected a non-negative integer`, `… expected an integer below 2^32`, `….copies[0].step: unknown field`, `….copies[0].instance: missing required field`, `….copies[0].feature: missing required field`, `….copies: expected an array`, `….copies[0]: expected an object`, `….attachment: an end cap is not named by a path edge`, `… a side face's path edge must be a valid entity`, `….face.along: expected an ID`, `… a side face is not named by an edge reference`, `… a chamfer face is named by its edge reference, from 1`, `….face.edge: expected a non-negative integer`, `… a hole bottom is not named by an entity`, `….face.role: unknown value 'bottom'` |

In every failure the failed sketch keeps its placement and its dependents
keep no result. After undo, the model regenerates to its analytic values.

## Tests

24 new Catch2 test cases, all tagged `[p12]`, and 3 process tests:

- `tests/core/geometry/FaceNameTests.cpp`: 4 new cases. They cover
  revolutions (partial and full turns, a segment on the axis), sweeps (the
  sides along each path segment, a clockwise profile, a closed path), lofts,
  and holes, chamfers and fillets. A new section of
  `FaceNames_PrismNamesItsCapsAndSides` covers moved copies and renaming.
- `tests/features/SketchOnFaceTests.cpp`: 13 cases. One for each face kind
  following its parameters; revolve directions; pattern, mirror and datum
  copies; circular copies; dependencies; removed and curved faces; refused
  roles and copies; validation; determinism.
- `tests/io/FaceKindFileTests.cpp`: 5 cases. Round trips of all seven
  models, the JSON form, malformed files, the example file, and STEP.
- `tests/cli/FaceReferenceCliTests.cpp`: 2 new cases (`info` for every kind
  and for copies, `validate`).
- `cli.info.post-row`, `cli.validate.post-row` and `cli.export-step.post-row`
  run on `examples/models/post_row.bcad`. Each is a fresh process that
  loads the file, resolves the copies' references and prints each body's
  volume and bounds.

Two `P12-STREF-001` test sections changed, by design:

- `FaceNames_PrismNamesItsCapsAndSides` expected moved copies to drop
  names. Transforms now carry them, so the moved box left the "no names"
  list, and a new section checks the names on the moved faces.
- `FaceReference_FailuresAreStructuredAndBlockDependents` used a linear
  pattern as "a kind that does not name its faces". A pattern's faces are
  now copies, so the section checks the new message instead.

No P0–P11 test changed.

The Release run of the 44 reference and naming cases records **9846
passed assertions and 0 failed** (`reference-values-release.txt`).

## Qualification

`qualify.cmd` was run through `run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output (the first attempt succeeded for each);
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

**This is the second run.** A first run, on the same tree except for one
check in `SketchOnFace_LoftCapsFollowTheSectionOffsets`, passed 919/919 in
all three presets and 711 × 5 in the repeats.

Reviewing its measured values showed a weak check. The kernel pads a
loft's bounds by 1e-7 mm, and the frustum's bounds sat exactly at the
±1e-7 mm tolerance of an exact-bounds check, which they passed only by
rounding. That check now uses the containment check of `LoftFeatureTests`:
the exact box must lie inside the kernel's box, which may exceed it by at
most the padding. The whole qualification was then run again, and every
number below comes from that second run. The two runs differ only in the
`tests` tree.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 303 | 0 | **919/919 passed** (108.1 s) |
| Release | exit 0 | attempt 1 | exit 0 | 303 | 0 | **919/919 passed** (92.4 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 303 | 0 | **919/919 passed** (107.2 s) |

303 is `P12-STREF-001`'s 301 translation units plus the two new test files.

**Repeats.** The change touches every sweep, blend, hole, transform and
pattern, so the selection is broad:
`ctest -R "[Ff]ace|[Rr]eference|[Nn]ame|[Cc]hamfer|[Ff]illet|[Dd]atum|[Ss]ketch|[Pp]rofile|[Ee]xtru|[Bb]oolean|[Hh]ole|[Mm]irror|[Cc]ircular|[Ll]inear|[Pp]attern|[Rr]evol|[Ss]weep|[Ll]oft|Curved|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ff]ile|cli|post-row|P9" --repeat until-fail:5`
selected 711 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 711/711, each 5 times (3555 passed runs) | 317.9 s |
| Debug | 711/711, each 5 times (3555 passed runs) | 251.2 s |

**Determinism.**

- `SketchOnFace_RegeneratesDeterministically` builds each of the seven
  models twice. It gets bit-identical placements and volumes, equal face
  names on every body, and equivalent documents. A second pass rebuilds
  nothing, and a full rebuild gives the same bits.
- `FaceKindFile_SaveLoad_ResolvesEveryFaceBitForBit` saves and loads each
  model. It overwrites the loaded placements of every attached sketch before
  regenerating. Every placement and volume comes back bit for bit, and the
  document serializes to identical text.
- The three process tests resolve the references in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 44 cases
  (5574 lines, MD5 `787266c048f418659c87ee750f9676ba`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-STREF-001 → P12-SKETCH-003, with 27 more tests:

| Preset | P12-STREF-001 | P12-SKETCH-003 |
| --- | --- | --- |
| Release | 602.9 s | 630.3 s |
| Debug | 648.4 s | 688.6 s |
| Debug-shared | 578.5 s | 634.4 s |

The 27 new tests take 21.1 s of the Release total. The twelve slowest tests
moved by up to 0.9 s each in Release (`compile_fail.ids.id-to-integer`,
7.23 → 6.32 s), 1.0 s in Debug
(`ReferenceModel_MountingBracketRegeneratesAfterParameterChanges`,
9.04 → 9.99 s) and 1.4 s in Debug-shared
(`CircularPattern_TouchingAndOverlappingInstancesFuse`, 7.85 → 6.46 s), in
both directions. The first run's totals were 632.7, 667.2 and 639.2 s. No
systematic change is claimed either way.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-stref001.txt` with `../P12-STREF-001/ctest-release.log`
(891 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 27 tests (181 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-STREF-001 Release build (the
  `8dbb722` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 24 new cases and the two changed ones, and normalizing
  temporary paths, document UUIDs, the reported Git revision and the source
  line numbers, the 28523 measured-value lines of the 837 compared cases
  are identical (MD5 `914f10877ea243a182833b77a617246c` both).
- Without normalizing line numbers, 71 lines differ. Each of them differs
  only in the line number of its assertion, moved by this milestone's edits
  to three test files (`FaceNameTests.cpp`, `FaceReferenceCliTests.cpp`,
  `FaceReferenceTests.cpp`). `compare-values.py` gained
  `--ignore-line-numbers` for this.
- `changed-tests-values.txt` shows every difference in the two changed
  cases: the added moved-copy section, and the pattern's new message.
- Names change no geometry: every volume, area, centre, bound and message of
  the other tests is the same, bit for bit.

**The P0–P12-STREF-001 regression suite remains green.** The only changes
to existing tests are the two `P12-STREF-001` sections above. Changes to existing
production code:

- `References`: the new roles, `along`, `edge`, `copies`,
  `referencedObjects()`;
- `OcctSweeps`: names for revolutions, sweeps and lofts
  (`PrismFace` became `SweptFace`);
- `OcctHole`, `OcctChamfer`: namers;
- `OcctBlend`, `OcctFillet`: names carried through blends;
- `OcctTransform`: names carried through transforms;
- `Faces`: `renameFaces()`;
- `SolidSupport`: the shared namer and `appendCopy()`;
- the regeneration of extrudes, revolves, sweeps, lofts, holes and chamfers:
  names;
- `PatternSupport` and the pattern and mirror regenerations: copies;
- `Sketch`, `Datums`: dependencies on copying features;
- `FaceReferences`: kinds, roles, copies;
- `Validation`, `DatumJson`, CLI `info`: the new fields. The CLI no longer
  lists an attachment's copying features as drivers.
- `.gitignore` now ignores `__pycache__/`. The `.pyc` file that the
  `P12-STREF-001` commit included by mistake is removed.

## Known Limitations

- **Names do not survive later features.** A reference is resolved in the
  body of the feature that made or last copied the face. Later features do
  not move it or remove it.
- **Fillets name nothing**, and neither do a loft's ruled sides.
- **A sweep's sides are named only where the kernel reports one face per
  path segment** for each profile edge (every sweep the tests build). A
  sweep for which it does not gets no side names, and references to its
  sides fail with NotFound.
- **A copy's instance is its index in the pattern.** Changing a pattern's
  count or spacing keeps a reference on the same index, and when an index is
  no longer made, the reference fails.
- **Undo after a change to a sketch-driving parameter** restores the model
  to rounding, not bit for bit (the sketch is re-solved).
- **The sketch frame on a face is a rule, not the feature's frame**
  (`P12-STREF-001`). A face turning through 45° to a model axis switches the
  projected axis: the ring's caps are checked beyond 45°, and the hub's copy
  within 45° of 90°.
- **Mirror planes, holes, chamfers and fillets** keep their geometric face
  and edge references (P11).

## History: the Blocker Record

On 2026-09-17, at `fe20b0f`, this milestone stopped before any
implementation (commit `9b74f75`). A face's only persistent reference was
its geometric signature, and that matches nothing once a parameter moves the
face.

`face_reference_reproducer.cpp` and `face-reference-reproducer.log` showed
this. Base's top face, referenced by its plane at z = 20, matched no face
after `height` 20 → 40. The nearest parallel face, a guess, was another
feature's (Step's top at z = 35), not Base's (at z = 40).

The record proposed a minimal stable face-reference layer: provenance at
generation, propagation through booleans, and references by feature and
role, with no fallback. The owner authorized it as `P12-STREF-001`, and this
milestone is built on it. The full record is in `9b74f75`.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-stref001.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`, `changed-tests-values.txt`: every measured
  value of the P12-STREF-001 tests, against the P12-STREF-001 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the
  measured values of the reference and naming tests.
- `values-determinism.txt`, `compare-values.py`: those values from all three
  configurations.
- `deviations.txt`, `deviations.py`: the largest deviations in the table
  above, with their cases.
- `timing-comparison.txt`, `compare-times.py`: per-test times.
- `kernel-probe/generation_history_probe.cpp`,
  `kernel-probe/generation-history-probe.log`: the kernel's history for
  revolutions, pipe shells, lofts, hole cutters, blends and transforms.
- `face_reference_reproducer.cpp`, `face-reference-reproducer.log`: the
  blocker's reproducer.

## Final Result

```text
TASK:            P12-SKETCH-003 Sketches on arbitrary planar faces
IMPLEMENTATION:  face names for revolves, sweeps (by path edge), lofts, hole
                 bottoms and counterbore floors, chamfer faces; copies named
                 by patterns and mirrors; names carried through blends and
                 transforms; references to copies (dependencies, resolution
                 in the copying feature's body); validation, JSON, CLI
TESTS:           24 new test cases and 3 process tests; 919/919 in Debug,
                 Release, Debug-shared; 711 related tests x5 in Release and
                 Debug
VALIDATION:      sketch frames on every kind of face within 1.4e-14 mm and
                 1.1e-16 of the parameters; bodies and bosses within 1.1e-15
                 rel (volume), 1.2e-10 mm (centre), 1.4e-14 mm (bounds);
                 copies followed their instance and failed when it went;
                 every existing measured value unchanged; values identical
                 across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-SKETCH-003/
TODO:            P12-SKETCH-003 deliverables ticked
```
