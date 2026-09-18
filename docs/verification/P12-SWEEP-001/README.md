# P12-SWEEP-001 — Guide Curves, Twist, Non-Planar Paths

```text
TASK:            P12-SWEEP-001
SCOPE:           paths that run through several sketches and so leave any one
                 plane; a twist law the section follows; guide curves that
                 carry it
IMPLEMENTATION:  geometry::SweptPath and its planner, the rotation-minimizing
                 frame, SweepPath::runs, SweepDefinition::twist and ::guide
TESTS:           1146 tests per preset, 100% passed in Debug, Release and
                 Debug-shared; 35 new cases; 918 x 5 repeats in Release and
                 Debug; 0 compiler warnings in 369 translation units
VALIDATION:      Pappus volumes, the extents a section turned by theta
                 reaches, and the twist angle measured from slices of the
                 solid, all computed without the sweep implementation
RESULT:          PASS
EVIDENCE:        this directory
REVISION:        qualified on the tree committed as this milestone; the eight
                 tree IDs are listed under Final Result
```

The three things `P11-FEAT-008` left out. Each is engineering intent the
definition stores, not geometry inferred after regeneration.

## Sweep Frame Definition

**One convention, measured rather than assumed.** `P11-FEAT-008` swept with a
fixed binormal — the path plane's normal — and did not say what should happen
when the path leaves that plane. Before choosing, the kernel was probed
(`kernel-probe/sweep_frame_probe.cpp`, log in
`kernel-probe/sweep-frame-probe.log`). What it measured:

| Case | Path | Fixed binormal | Frenet | Corrected Frenet |
| --- | --- | --- | --- | --- |
| G | planar: line, tangent arc, line | valid, V/Pappus **1.000000000** | valid, **1.000000000** | valid, **1.000000000** |
| C | two collinear straight segments | valid, 1.000000000 | valid, 1.000000000 | valid, 1.000000000 |
| B | spatial polyline, two right corners | **throws** `gp_VectorWithNullMagnitude` | valid, 1.000000000 | valid, **1.000000000** |
| A | helix, radius 20, half a turn | valid, 1.0000027 | valid, 0.9999982 | valid, 1.00000018 |

On a planar path the three frames give the **same solid to the last digit**,
and the same section orientation. So BetterCAD has one convention — the
**rotation-minimizing frame**, which adds no turn about the tangent — and
`P11-FEAT-008`'s fixed binormal is its closed-form planar case. On a spatial
path the fixed binormal fails as soon as a run runs parallel to it, which is
exactly why it cannot be kept; the kernel's corrected Frenet mode
(`SetMode(false)`) carries the frame there, and was exact on the polyline.

`SweepFrame` records which is in use: `PlanarBinormal` (one run, no twist or
guide), `RotationMinimizing` (several runs), `Guided` (a twist or a guide).
A one-run path with neither is handed to `makeSweep(region, PlanarPath)`
unchanged, so every solid `P11-FEAT-008` built is built by the same code and
is bit-for-bit what it was.

A fixed trihedron (`SetMode(gp_Ax2)`) was measured too and rejected: on the
helix it enclosed 0.288 of the Pappus volume, because it does not keep the
section across the tangent.

## Non-Planar Path Semantics

A path is one or more **runs**, each the edges of one sketch, joined end to
end in model space. One run is the planar path of `P11-FEAT-008`; several, on
different planes, make a spatial path.

**No 3D-sketch subsystem is introduced.** Every run is still a planar sketch
with its own placement, its own constraints and its own parameters; only the
joins between runs happen in model space. That is the smallest abstraction
that makes a spatial path possible with the document model as it stands.

Every rule of a planar path holds across the runs, applied in model space by
`detail::planSweptPath()`:

- connected: consecutive segments meet to 1e-10 m, whichever sketches drew
  them;
- ordered and non-degenerate: no zero-length segment, no arc whose ends
  coincide or whose ends are different distances from its centre, no circle
  of no radius;
- finite: every coordinate, and every run's plane origin;
- unambiguously oriented: the direction of travel is fixed by the edges, as
  before;
- jointed: segments meet tangentially (sine ≤ 1e-9), or two straight segments
  meet at a mitred corner of less than 180°. An arc at a corner, and a path
  that turns back on itself, are refused;
- a full circle is a closed path on its own, and then the only segment of the
  only run;
- ellipses and splines are refused, as in a planar path.

**Two rules a turning section adds**, for spatial and for twisted or guided
sweeps:

- **the profile's centroid must ride on the path.** The section then travels
  exactly the path's length whatever the frame does, so the volume is the
  area times that length however the section turns, and the Pappus check does
  not depend on the frame. A profile centred elsewhere is refused with the
  distance it is out by;
- **the reach replaces the signed offset.** A turning section has no constant
  offset from the path, so the fold checks use the *reach* — how far the
  farthest point of the profile lies from the path. An arc's radius and a
  mitred corner's legs must both exceed it. The reach is computed from the
  loop's own geometry and never under-estimates (a curve is bounded by its
  centre and radius, a spline by its poles), so a fold check made with it
  never passes geometry that folds.

**Face names across runs.** A side face names the profile entity that swept
it and the path edge it ran along. Entity IDs are numbered *per sketch*, so
on a multi-run path two runs' edges can share an ID; the edge alone would not
say which run. `FaceSelector::alongSketch` therefore names the sketch too,
whenever the path has more than one run. A path in one sketch is named
exactly as it was, and a file written before this milestone has no
`along_sketch` and needs none.

## Twist Law

```text
theta(u) = u * theta_total,   u in [0, 1] along the path
```

`u` is the normalized path coordinate, 0 at the start and 1 at the end.
`theta_total` is `SweepDefinition::twist`, literal or driven by an angle
parameter. The angle is measured **from the profile sketch's own X axis**,
positive right-handed about the direction of travel, so a test can predict
where any point of the section lies at any `u` without asking the sweep.

**The law is BetterCAD's, not the kernel's.** `BRepOffsetAPI_MakePipeShell`
offers a *scaling* law and no rotation law at all (probe case F, a recorded
negative result). So BetterCAD builds the auxiliary spine itself:
`detail::samplePath()` walks the path at equal arc length carrying its own
rotation-minimizing frame by the double-reflection method, each sample is
offset by the profile's reach turned by `theta(u)`, and a B-spline is fitted
through the samples and handed to the kernel as the auxiliary spine.

Zero twist uses no auxiliary spine at all, so an untwisted sweep is the sweep
it always was.

## Guide-Curve Semantics

A guide is **a path of its own** — the same `SweepPath`, with its own runs —
whose turning about the path carries the section. What it controls, exactly:
at each point of the path, the section's reference direction points from the
path towards the guide. The guide's distance from the path is not used; only
its direction is, so a guide that approaches or recedes changes nothing.

The kernel mode was chosen on measured evidence (probe case D, every
combination tried):

| Curvilinear equivalence | Contact | Result |
| --- | --- | --- |
| no | `NoContact` | **valid, V/Pappus 1.0000019** |
| no | `Contact` | not done |
| no | `ContactOnBorder` | throws `Standard_ConstructionError` |
| yes | `NoContact` | valid, but 1.0050 — the section is scaled |
| yes | `Contact` / `ContactOnBorder` | not done |

So `BRepFill_NoContact` without curvilinear equivalence. The API takes one
auxiliary spine, so **a sweep takes one guide**; that is a limitation of the
kernel's interface, recorded rather than worked around.

**A guide and a twist are mutually exclusive.** Both say how the section
turns, and the kernel has one auxiliary spine to say it with. A definition
holding both is refused outright.

A guide is checked as a *curve* (`detail::planPathCurve()`): connected,
non-degenerate, finite, and jointed. Nothing sits on it — it carries the
section rather than supporting it — so it has no profile and no placement
rule of its own.

## Implementation

| File | Change |
| --- | --- |
| `include/bettercad/core/geometry/Sweeps.hpp` | `SweptPath` (runs, twist, guide), `asSweptPath()`, `SweepFrame`, `frameOf()`, the two new `makeSweep()` overloads |
| `src/core/geometry/SweptPathPlan.hpp`, `.cpp` | the kernel-independent planner: model-space segments, the path and guide checks, the centroid and reach rules, `samplePath()` |
| `src/core/geometry/occt/OcctSweeps.cpp` | the spatial spine, the frame modes, the generated twist spine, the guided sweep and its Pappus check |
| `include/bettercad/features/SweepFeature.hpp`, `src/features/sweep/SweepFeature.cpp` | `SweepPathRun`, `SweepPath::runs`, `twist`, `twistParameter`, `guide`, their validation and the dependencies |
| `src/features/sweep/SweepRegeneration.cpp` | `resolveSweptPath()`, the path edge each side face runs along |
| `include/bettercad/core/document/References.hpp`, `src/core/document/References.cpp` | `FaceSelector::alongSketch` and its rules |
| `src/features/SolidSupport.hpp`, `.cpp` | `PathEdge` (sketch and edge) for the namer |
| `src/features/reference/FaceReferences.cpp` | a side's edge checked against every run; the sketch required when there are several |
| `src/io/json/FeatureJson.cpp`, `src/io/json/DatumJson.cpp` | `runs`, `twist`, `twist_parameter`, `guide`, `along_sketch` |
| `src/features/Validation.cpp` | every run of the path and of the guide checked |
| `apps/bettercad_cli/DocumentCommands.cpp` | the description |

## Tests

35 new test cases, all passing in Debug, Release and Debug-shared.

| File | Cases | What they cover |
| --- | ---: | --- |
| `tests/core/geometry/SweptPathTests.cpp` | 10 | A one-run path being the planar sweep bit for bit; runs on different planes; a spatial path bending through an arc; malformed spatial paths; twist at 0, 45, 90, 180, 360 and -90 degrees; the twist law measured from slices; twist on a curved path; a non-finite twist; a guide curve; an unusable guide |
| `tests/features/SpatialSweepTests.cpp` | 14 | Runs from several sketches as one path; the dependencies; side faces named across runs; runs that do not meet; invalid runs refused outright; the twist through its parameter and through its path; invalid twists; a guide carrying the section; a guide and a twist together; an unusable guide; atomic failure and recovery; undo and redo; determinism for all three capabilities |
| `tests/io/SpatialSweepFileTests.cpp` | 6 | Round trip of every run, of a driven and a literal twist, and of a guide; a file written before this milestone read and written back byte for byte; bit-for-bit rebuilds from file; STEP export and read-back |
| `tests/cli/SpatialSweepCliTests.cpp` | 5 | `info` naming every run, describing a driven and a literal twist, saying nothing when there is none, and naming the guide; `validate` on a spatial sweep and on a disconnected path |

The milestone's own selector (`[sweep][p12]`) covers 36 test cases — the 35
new ones and one earlier sweep test that carries both tags.

One `P11-FEAT-008` assertion changed deliberately:
`SweepFeature_MalformedDataIsRejectedWithTheJsonPath` asserted that
`data.twist` was an *unknown field*. This milestone adds it, so that
assertion became its opposite, and malformed cases for each new field were
added beside it. One more was corrected rather than changed: it asserted that
an edge ID repeated in another run was "listed twice in the path", which is
wrong now that runs are separate sketches — the same ID in another sketch is
another edge. A repeat *within* one run is still refused, and is tested.

## Independent Validation

Every expected number is derived from the definition by hand. The production
sweep is never asked what the answer should be.

| Check | Reference | Where |
| --- | --- | --- |
| Spatial path volume | Pappus: area × length, 16 mm² × 120 mm | `SweptPath_RunsOnDifferentPlanesMakeASpatialPath`, `SpatialSweep_RunsFromSeveralSketchesMakeOnePath` |
| Spatial path bounds | 2 mm either side of each run; the caps stop it at z = 0 and y = 40 | same |
| Arc-and-line spatial path | 4π mm² × (20·π/2 + 30) mm | `SweptPath_ASpatialPathMayBendThroughAnArc` |
| One run = the planar sweep | the same volume, area, bounds and topology, **bit for bit** | `SweptPath_OneRunIsThePlanarSweepItself` |
| Twisted volume | area × length, unchanged by the turn | every twist test |
| Twisted extents | a 2a × 2b section turned by φ reaches a\|cos φ\| + b\|sin φ\| and a\|sin φ\| + b\|cos φ\|; over φ ∈ [0, θ] the solid reaches the largest of those | `SweptPath_TwistTurnsTheSectionAboutTheTangent`, `SpatialSweep_TwistTurnsTheSectionByTheAngleGiven` |
| θ(u) = u·θ_total | a 0.1 mm slab cut from the solid at u = 0, ¼, ½, ¾, 1 and its bounds compared with a\|cos uθ\| + b\|sin uθ\| | `SweptPath_TwistIsProportionalToTheDistanceAlongThePath` |
| Twist on a curve | area × (40·π/2) mm, and the solid staying within the section's radius of the 40 mm circle | `SweptPath_TwistsAlongACurvedPath` |
| Guide correspondence | a guide whose offset turns from +X to +Y is a quarter turn by its own definition, so the section must reach its radius both ways | `SweptPath_AGuideCurveCarriesTheSection`, `SpatialSweep_AGuideCurveCarriesTheSection` |
| Frame composition | the kernel probe's four frames against each other and against Pappus | `kernel-probe/sweep-frame-probe.log` |
| STEP | the exported solid read back by the kernel and measured | `SpatialSweep_ExportsStep` |

## Stable References

- A side face names the profile entity and the path edge; with several runs
  it names that edge's **sketch** too, so two runs' edges that share an ID are
  told apart. `SpatialSweep_SidesAreNamedByTheEdgeTheyRunAlong` asserts that
  the model's three runs really do share an edge ID
  (`CHECK(m.riseLine == m.crossLine)`) and that each (sketch, edge) pair still
  names **exactly one** face — twelve in all, four profile lines along each of
  three path edges.
- An edge of no run names nothing: `findNamedFaces()` returns an empty list
  and nothing is substituted.
- `checkFaceName()` refuses a side that omits the sketch when the path has
  several runs, and refuses an edge that belongs to none of them (`NotFound`).
- No face reference holds a kernel index, a `TopoDS` pointer or a traversal
  position at any point.

## Failure Paths

Every one fails with a structured `Error`, keeps no body, and leaves the
document unchanged.

| Input | Code | Message |
| --- | --- | --- |
| a run whose sketch is missing | `NotFound` | `path ... is not a sketch in this document` |
| a run without a sketch | `InvalidArgument` | `path run 2 needs a sketch` |
| a run without edges | `InvalidArgument` | `path run 3 needs at least one edge` |
| an edge listed twice in one run | `InvalidArgument` | `entity:N is listed twice in the path` |
| runs that do not meet | `InvalidArgument` | `the path is not connected: segment 1 ends at ..., but segment 2 starts at ..., 5 mm away` |
| a run that turns back | `InvalidArgument` | `the path turns back on itself where segments 1 and 2 meet` |
| an arc at a corner | `InvalidArgument` | `path segments 1 (an arc) and 2 (a line) meet at an angle of ...` |
| an empty run | `InvalidArgument` | `the path has an empty run 2` |
| an empty path | `InvalidArgument` | `the path is empty` |
| a profile whose centroid is off the path | `InvalidArgument` | `the profile's centroid must lie on the path, but it lies 10 mm from where the path starts: ...` |
| a profile too wide for an arc | `InvalidArgument` | `the profile reaches ... towards the centre of path segment N ...: the swept solid would fold over itself` |
| a straight run too short for its mitres | `InvalidArgument` | `path segment N is too short for the mitred corners at its ends ...` |
| a non-finite twist | `InvalidArgument` | `the twist must be finite, got nan deg` |
| a twist parameter of the wrong kind | `InvalidArgument` | `parameter 'length' has dimension length, not angle` |
| a twist parameter that does not exist | `NotFound` | the dependency graph reports the missing parameter |
| a twist and a guide together | `InvalidArgument` | `a sweep takes a twist or a guide curve, not both: a guide already says how the section turns` |
| a guide in the profile's sketch | `InvalidArgument` | `the guide must be in another sketch than the profile` |
| a guide whose runs do not meet | `InvalidArgument` | `the guide is not connected: ...` |
| a guide segment of no length | `InvalidArgument` | `the guide segment 1 is a point at ...` |
| a guide edge that is not in its sketch | `NotFound` | `the path edge ... does not exist in sketch '...'` |
| a self-intersecting result | `InvalidArgument` | `the swept solid would intersect itself ...` |
| a kernel failure | `Internal` | `the kernel could not sweep the profile along the path` |

**Atomicity.** The solid is built into a local body and returned only when
every check has passed: one valid solid, no self-interference, a finite
positive volume, and the Pappus volume within tolerance. A failure returns the
error, so no partially built body can become the document's result; the
failing feature keeps none and the features below it keep theirs.
`SpatialSweep_FailuresKeepNoBodyAndRecover` drives a sweep into failure and
back, and the recovered body is bit-for-bit the original.

## Persistence

New fields, written only when they are not their default, so a sweep written
before this milestone is written back **byte for byte**:

```json
"path": { "sketch": 2, "edges": [1], "runs": [ { "sketch": 3, "edges": [1] } ] },
"twist": 1.5707963267948966,
"twist_parameter": 1,
"guide": { "sketch": 3, "edges": [1] }
```

and on a face reference, `"along_sketch": id`.

`SpatialSweep_AFileWithoutTheNewFieldsIsAPlanarSweep` saves a
`P11-FEAT-008` sweep, checks the file has none of `runs`, `twist`, `guide` or
`along_sketch`, loads it, checks the definition reads as one run with no twist
and no guide, and compares the bytes of the file written back.

`SpatialSweep_SaveLoadPreserves…` round-trips each capability: build → save →
destroy → load → regenerate, comparing the definition field by field, the
resolved path run for run, the regeneration order, and the volume, surface
area, centroid, bounding box and topology **bit for bit**. The twist test then
changes the parameter on the loaded document and checks it still drives the
feature — the intent survived, not just the shape.

## Determinism

**Across configurations.** The milestone's tests ran in all three presets
with `-s --rng-seed 1`; `values.py` kept every measured value and
`compare-values.py` compared them
(`qualification/values-determinism.txt`):

```text
release:      1839 non-empty lines, MD5 0e5fd4d86080d60737f176de6380efab
debug:        1839 non-empty lines, MD5 83d880903ac3b1613e3d0887a813f68d
debug-shared: 1839 non-empty lines, MD5 83d880903ac3b1613e3d0887a813f68d
differing lines: 1 expected only, 0 measured
RESULT: measured values identical
```

**The one differing line is not a determinism failure, and is recorded
rather than hidden.** Debug and Debug-shared agree exactly; Release differs
in a single line, and in that line the *measured* value is identical:

```text
release: => 3.53553400593273759 is within 0.002 of 3.53553390593273731
debug:   => 3.53553400593273759 is within 0.002 of 3.53553390593273775
```

The number that moved is the *expected* one, `(a + b) / sqrt(2)` computed by
the test itself, which the optimiser folds differently at `-O2`. It differs
by 4.4e-16 relative, inside a 0.002 mm tolerance, and BetterCAD's own output
(`3.53553400593273759`) is bit-identical in all three configurations. The
comparison tool classifies it exactly so: **0 measured differences**.

**Within a run.** `SpatialSweep_RegenerationIsDeterministic` builds a
spatial path, a twisted sweep and a guided sweep, and compares the volume,
surface area and three centroid components of the document regenerated once,
the same document regenerated again, and a second document built the same
way. The comparison is `==` on the SI doubles, not a tolerance.

**Repeats.** The 918 tests the milestone's selector covers ran five times
each in Release and in Debug (`--repeat until-fail:5`): 100 % passed.

**Through the file.** `SpatialSweep_RebuildsIdenticallyFromAFile` builds,
saves, destroys, loads and regenerates each capability and compares the
volume, surface area and centroid as raw bits (`std::bit_cast`), with the
bounding box and the topology.

## STEP Read-Back

`SpatialSweep_ExportsStep` exports a spatial path, a twisted sweep and a
guided sweep, reads each back with the kernel (test tooling only — STEP import
is not a product feature) and checks the read-back solid is one valid solid
whose volume matches the exported one to 1e-9 relative.

## Regression

**Every earlier test, by name, in all three presets**
(`qualification/regression-comparison.txt`, `-pattern001.txt`):

| Baseline | Names | Missing | Not passed | New |
| --- | ---: | ---: | ---: | ---: |
| `P11-QUAL-001` Release | 737 | 0 | 0 | 408 |
| `P12-PATTERN-001` Release | 1110 | 0 | 0 | 35 |

Both `RESULT: PASS`, for `ctest-debug.log`, `ctest-release.log` and
`ctest-debug-shared.log` alike. No test was removed or renamed.

**Every measured value of every earlier test**
(`qualification/all-values-comparison.txt`). `P12-PATTERN-001`'s commit
(`236f8c9`) was built in a separate clean worktree of that exact revision and
its whole suite run with the same options, then compared with this
milestone's qualified Release run:

```text
pattern001-release: 48381 non-empty lines, MD5 f35c35f45ee7a5814e2244a96aeacd22
sweep001-release:   48381 non-empty lines, MD5 f35c35f45ee7a5814e2244a96aeacd22
differing lines: 0 expected only, 0 measured
RESULT: measured values identical
```

**The two deliberate test changes**, both in
`SweepFeature_MalformedDataIsRejectedWithTheJsonPath`, which is excluded from
that comparison for exactly these reasons:

1. **`twist` is no longer an unknown JSON field.** `P11-FEAT-008` asserted
   `objects[4].data.twist: unknown field`, which was the correct statement
   that a sweep had no twist. This milestone adds one, so the assertion
   became its opposite: the same document now loads. Malformed cases were
   added beside it — a twist that is not a number, a bad `twist_parameter`,
   a malformed `guide`, a malformed `runs`, and a still-unknown field
   (`spin`) to show the reader is still closed.
2. **Identical `EntityId` values in different path-run sketches are valid.**
   The old assertion required an edge ID repeated in another run to fail
   with `entity:3 is listed twice in the path`. That is wrong once runs are
   separate sketches: entity IDs are numbered *per sketch*, so the same ID
   in another sketch is a different edge, and refusing it would refuse
   legitimate models. The test now asserts that such a document **loads**,
   and a repeat *within one run* is still refused and still tested.

A third case, `Failures are reported and block dependents until fixed`, is
excluded as it was for `P12-PATTERN-001`: its entries are pointer
comparisons, and `values.py` drops an entry repeated verbatim within a test
case, so whether the second occurrence survives depends on whether the heap
reused the same address. The assertion passed in both runs.

**Largest deviations** (`qualification/deviations.txt`). Across the
milestone's own checks:

| Group | Tolerance | Checks | Largest |
| --- | ---: | ---: | ---: |
| Untwisted geometry, absolute | 1e-9 mm | 16 | 4.44e-16 |
| Untwisted geometry, relative | 1e-12 | 6 | 4.42e-16 |
| Guided volume, relative | 1e-5 | 16 | **2.72e-06** |
| Guided position, absolute | 2e-3 mm | 27 | **2.74e-05** |
| Slab-measured twist angle | 2e-2 mm | 10 | 6.28e-03 |

The guided figures sit where the kernel probe predicted (1.9e-6), with about
four times' margin. No tolerance was loosened anywhere, and the untwisted
paths keep the 1e-9 and 1e-12 of `P11-FEAT-008`.

## Known Limitations

- **One guide.** `BRepOffsetAPI_MakePipeShell` takes one auxiliary spine, so
  a sweep takes one guide curve. Several guides would need a different
  builder and are not attempted.
- **A guide or a twist, never both.** They are two ways to say the same
  thing, and the kernel has one auxiliary spine to say it with.
- **A turning or spatial sweep needs its profile centred on the path.** The
  volume check would otherwise depend on the frame, which is the kernel's for
  a spatial path. A planar untwisted sweep keeps the freedom it had: its
  profile may sit anywhere in its plane.
- **A guided sweep meets Pappus to 1e-5, not 1e-9.** The auxiliary spine is a
  B-spline fitted through samples of the path; the measured worst departure
  is 1.9e-6. An untwisted sweep, planar or spatial, keeps the 1e-9 of an
  exact frame.
- **Runs are planar sketches.** There is no 3D sketch, no helix primitive, no
  spline path and no model-edge path. A helix must be approximated by runs,
  or reached through a guide curve, until a spatial-curve entity is
  authorized.
- **Ellipses and splines are still refused in a path**, as in
  `P11-FEAT-008`.
- **The twist law is linear in the arc length.** `theta(u) = u theta_total`
  and nothing else; there is no twist table, no easing and no per-segment
  twist.
- **Corners are still mitred, and only between two straight segments.** An
  arc must meet its neighbours tangentially, across runs as within one.
- **Patterns and mirrors still do not accept a sweep as a feature-scope
  source** (`P11-FEAT-008`), unchanged here.

## Evidence Files

| File | What |
| --- | --- |
| `kernel-probe/sweep_frame_probe.cpp`, `build-and-run.cmd` | The probe that measured the frame modes, the guide modes and the twist, and how it was built and run. |
| `kernel-probe/sweep-frame-probe.log` | Its run: eight cases, every frame and guide combination. |
| `qualification/qualify.cmd`, `run-qualification.cmd` | The qualification: clean rebuild and tests in each preset, then the repeats. |
| `qualification/configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log` | Each preset's configure, clean, build and test output. |
| `qualification/ctest-repeat-*.log` | The five-times repeats in Release and Debug. |
| `qualification/qualification-times.txt` | Times, exit codes and the Git tree IDs of what was built. |
| `qualification/reference-values-release.txt` | Every value the milestone's tests measured (Release, `-s`). |
| `qualification/values-determinism.txt` | The same values from all three presets, and their MD5. |
| `qualification/all-values-comparison.txt` | Every assertion of the whole suite, compared with `P12-PATTERN-001`. |
| `qualification/regression-comparison.txt`, `-pattern001.txt` | Every `P11` and `P12-PATTERN-001` test, by name, in all three presets. |
| `qualification/deviations.txt` | The largest deviation measured in each group of checks. |
| `qualification/timing-comparison.txt` | Per-test times against `P12-PATTERN-001`. |
| `qualification/values.py`, `compare-values.py`, `compare-regression.py`, `compare-times.py`, `deviations.py`, `timing-split.py`, `collect-evidence.cmd` | The comparison tools. |

## Final Result

```text
RESULT: PASS
```

| Gate | Result |
| --- | --- |
| Debug | 1146 / 1146 passed |
| Release | 1146 / 1146 passed |
| Debug-shared | 1146 / 1146 passed |
| Unexpected compiler warnings | 0, in 369 translation units per preset, `-Werror` |
| Clean rebuild | every preset cleaned (`ninja -t clean`) and rebuilt from nothing before its tests |
| `P12-SWEEP-001` tests | 35 / 35 cases passed, all three presets |
| Legacy tests (`P11`) | 737 / 737 names present and passed, all three presets |
| Earlier `P12` tests | 1110 / 1110 names present and passed, all three presets |
| Repeats | 918 tests x 5 in Release and in Debug, 100 % passed |
| Independent geometry validation | Pappus volumes, analytic section extents, slab-measured twist angles |
| Non-planar path validation | volume, bounds and centroid of a three-run spatial path and an arc-and-line path |
| Twist-law validation | theta(u) = u theta_total at u = 0, 1/4, 1/2, 3/4, 1, and at six total angles |
| Guide-curve validation | a guide whose turning is fixed by its own definition, at the geometry and the feature level |
| Stable references | every (sketch, edge) pair names exactly one face; the ambiguity the sketch resolves is asserted |
| Failure paths | 22 rows of the table above, each keeping no body |
| Serialization / save-load | every new field round-tripped; old files byte-identical |
| Determinism | identical measured values in all three presets; repeated, fresh and reloaded builds identical |
| STEP read-back | spatial, twisted and guided solids exported and read back, 1 solid each, volume to 1e-9 |
| Existing measured values | identical to `P12-PATTERN-001`'s (MD5 `f35c35f4...`), with the three excluded sets explained |

**Environment.** GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, Brecht Sanders
r4), C++23, `-Werror`; CMake 4.4.2, Ninja 1.13.2; OCCT 8.0.1; Catch2 3.16.0;
Qt 6.11.2; Windows 11 Pro 10.0.26200, AMD Ryzen 7 5800H.

**The tree that was qualified.** `qualification/qualification-times.txt`
records the Git tree IDs of the sources built, taken from a scratch index
before the first configure, and they were re-read from the working tree after
the qualification and again from the commit:

```text
apps              0247759f0a34433331618ee594e874b4d0f3c5e3
include           493f1649c2065e4d790396edff34cd2e95d20cdb
src               3bc7077c77dd49664ff579a3efe13e815dfbd3b8
tests             2559aa850685f7577e76f9262f65ec4726234872
examples          07f6ea93fbb93e97a4e6e989ef7636a612d26848
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Nothing under them changed between the qualification and the commit. The
kernel probe was built against OCCT directly and is evidence only; it is not
part of the build and is not in these trees.

## The Findings This Milestone Rests On

1. **Non-planar paths are several planar sketch runs.** BetterCAD did *not*
   introduce a general 3D-sketch subsystem. Every run is a planar sketch with
   its own placement, constraints and parameters; only the joins between runs
   happen in model space. That is the smallest abstraction that makes a
   spatial path possible with the document model as it stands.
2. **The twist law is `theta(u) = u theta_total`**, u the normalized path
   coordinate, measured from the profile sketch's own X axis, positive
   right-handed about the direction of travel. It is BetterCAD's law: the
   kernel offers a scaling law and no rotation law at all.
3. **Sweep orientation uses the rotation-minimizing frame**, verified rather
   than assumed: on a planar path the fixed binormal, Frenet and corrected
   Frenet frames give the same solid to the last digit (probe case G).
4. **`P11-FEAT-008`'s planar fixed-binormal behaviour is preserved as the
   planar case.** A one-run path with neither a twist nor a guide is handed
   to the original `makeSweep(region, PlanarPath)`, so those solids are built
   by the same code and are bit-for-bit what they were.
5. **Guide curves use only the independently verified kernel mode**:
   `BRepFill_NoContact` without curvilinear equivalence, the one combination
   the probe measured as building a valid solid that keeps the profile's
   area.
6. **`FaceSelector::along` was insufficient for multi-run paths**, because
   `EntityId` values are local to a sketch. Two runs' edges can share an ID,
   and one name then matched three faces — found by a test that expected 12
   faces and got 36.
7. **`FaceSelector::alongSketch` disambiguates a swept side** by naming both
   the sketch and the edge. A path in one sketch is named exactly as before
   and a file written before this milestone has no `along_sketch`, so nothing
   old changes meaning.
