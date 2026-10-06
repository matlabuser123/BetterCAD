# P16-REFMOD-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the suite before calling it complete
QUESTIONS: 25 from the brief, each answered against what was built
FINDINGS: 5 -- 2 production defects, 3 gaps in the suite itself
          all 5 resolved before the freeze
PRODUCTION DEFECTS: 2, both fixed
```

This is a gate, not a formality. Each question below was answered by reading
the code or by running something, not by assuming. Where the answer was "yes,
it could", the fix came before the box was ticked.

## Findings

### F1 — `ResolvedSizing::unresolvedCount()` is not exported (PRODUCTION DEFECT)

**Found by:** the `debug-shared-ext` build, as the fourth pre-freeze gate.

`ResolvedSizing` is a plain data struct in a public header with no export
macro, and `unresolvedCount()` was defined **out of line** in
`src/meshing/MeshSizing.cpp`. So the symbol is not exported from
`libbettercad_meshing.dll`, and any caller in another DLL gets

```text
undefined reference to `bettercad::meshing::ResolvedSizing::unresolvedCount() const'
```

It has been unusable across a DLL boundary since P16-SIZE-001, and nothing
noticed because nothing outside the meshing library had called it. This
milestone's suite is the first caller.

**Fix:** defined inline in the header, which is what every other predicate on a
report struct in this module already does — `VolumeConformity::conforms()`,
`MeshValidationReport::dataValid()`,
`GeometryMeshMappingReport::complete()`, `BoundaryFacetSet::fullyResolved()`.
The out-of-line definition is removed. The header now records why it has to be
inline.

**Not worked around.** Counting the unresolved controls in the test instead
would have left a public, advertised API unusable from a GUI in another DLL,
which is the same product defect with the evidence hidden.

### F2 — `MeshControl::kTypeName` at runtime does not link shared (CONVENTION)

**Found by:** the same build.

```text
undefined reference to `__imp__ZN9bettercad7meshing11MeshControl9kTypeNameE'
```

Binding a reference to a dll-imported `static constexpr` member does not link.
The repository already has a convention for this, recorded in
`MaterialTests.cpp` and `DocumentJson.cpp`: compare against the **literal**, and
keep the two in step with a `static_assert`. `MeshControlJson.cpp` already
carries that assert for `"mesh-control"`.

**Fix:** the suite compares against the literal, with the reason written at the
site. It linked in both static presets and failed only in `debug-shared-ext` —
which is the pre-freeze ordering earning its place for the second milestone
running.

### F3 — the convergence table passed on floating-point noise (SUITE DEFECT)

**Found by:** reading the measured table rather than the verdict.

RM-MESH-02's convergence study first used deflections of 0.5, 0.25 and 0.1 mm.
Measured, **0.5 and 0.25 produced the same 140 boundary facets** — OCCT's
20-degree angular limit was binding at both — so the two volumes differed only
in their last two digits:

```text
coarse  117212.51992517796
medium  117212.51992517807
```

The assertion `errors[1] < errors[0]` therefore passed on one unit in the last
place. It would have gone into the evidence as convergence.

**Fix:** the suite now asserts that each level has **strictly more boundary
facets** than the last, *before* comparing any errors, and the levels were
changed to 0.5, 0.1 and 0.02 mm, which genuinely differ (140, 196, 444 facets).
The error now falls by a factor of ten rather than by a rounding, and the gate
requires at least 1.5x per step.

### F4 — RM-MESH-01's reverse mapping was a spot check (SUITE GAP)

**Found by:** question 3 below.

The six-face partition test resolved every face's facets and checked the plane
of every facet, but asked `sourceFaceOf` about only the **first** facet of each
face. A map that attributed one facet of a face correctly and the rest to a
neighbour would have passed.

**Fix:** every facet is now asked in reverse, and its own handle is checked to
come back.

### F5 — RM-MESH-06's mapping check was vacuous, and the deflection was unverified (SUITE GAPS)

**Found by:** questions 5 and the RM-MESH-02 conformity requirement.

Two separate holes:

* The transformed model's mapping check computed a `WallOrientation` and then
  asserted only that it had counted the facets — a statement with no content.
  **Fix:** the mapped facets' normals are now checked against `R` applied to the
  base model's normal, with the base's own normal checked to be `-Z` and the
  transformed one checked **not** to be any coordinate axis. This is the
  mapping half of the frame check, and mutation M4 fired on it twice.
* RM-MESH-02 checked that every lateral node lies **on** the cylinder, which is
  true at any deflection — so nothing verified that the **declared** deflection
  meant anything. **Fix:** the chord sagitta is now measured facet edge by
  facet edge; measured 0.0951325 mm against the declared 0.25 mm, which is also
  exactly what `r(1 - cos 5 degrees)` predicts for the 36 segments the kernel
  produced.

## The 25 questions

**Can a symmetric cube hide axis-swapping defects?**
No cube exists in the suite: 120x70x35, 25/60, 100x60x12, 30/18/45, 120x80x1.5,
90x55x24, 100x60x40. RM-MESH-01's test asserts `a != b != c` rather than
trusting the builder, and the bounding box is checked axis by axis against the
right dimension. **Mutation M1 makes it a cube: killed on 4 assertions.**

**Can analytical expected volume come from production CAD-volume code?**
Structurally impossible: `tests/reference/Analytic.hpp` includes no BetterCAD
and no OCCT header, so it cannot call one. `analyticVolumeOf` reads length
**parameters** and multiplies them; nothing in it reads a volume.

**Can RM-MESH-03 fill the hole but still pass total volume accidentally?**
No — and three independent gates say so. A filled hole reads 72000 mm^3, which
exceeds the analytic bound's upper limit by **29.32 times the bound's own
width**; the independent occupancy check finds nodes and centroids inside the
void; and the hole-wall mapping stops resolving. **Mutation M2: killed on 11
assertions, with the volume, occupancy and mapping gates each firing.**

**Can hollow-tube inner and outer walls map to the same CAD face?**
They are swept by different circles, so they carry different `FaceName`s. The
suite checks pairwise facet disjointness, the node radii (every outer node at
Ro, every inner node at Ri) and the normal directions. **Mutation M3 swaps them:
killed — and notably the disjointness checks did NOT fire, because the facets
are still disjoint. The radii and the normals caught it.**

**Can transformed model quality change due to a coordinate-frame bug?**
The dimensionless metrics are compared at 1e-4 relative, a tolerance **derived**
from the measured interior-node discrepancy rather than chosen. On a 14.93
degree dihedral that gate allows 0.0015 degrees, so a frame defect that moved
the angle by even a tenth of a degree would fail it by a factor of 67.

**Can transformed mesh remain untransformed while counts still match?**
No: the placed bounding box must be more than 1 mm from the base's (measured
33.6 mm), the boundary node set must match `R x + t` to 1e-9 mm, and the mapped
facet normals must match `R` applied to the base normal. **Mutation M4 removes the
transform: killed on 5 assertions.**

**Can local-refinement qualification pass only because total Tet count
increased?** The total count is reported and never asserted. The gates are the
band's characteristic scale, the band's node density, and the mirror.
**Mutation M5 refines the whole body: the "target is refined" assertions did not
fire; the mirror and the slab position killed it on 8.**

**Can local sizing accidentally refine the entire model?** Same mutation, same
answer. The mirror compares the band at F between a mesh refining F and a mesh
refining G, in both directions, so a uniform refinement fails one of them.

**Can RM-MESH-05 generate inverted cells and still PASS because warnings are
ignored?** The suite counts negative and zero signed volumes itself, from a
determinant per element, and requires zero of each — independently of
`generateVolumeMesh`'s own refusal. `invalid = 0` is asserted separately.
RM-MESH-05's elements are genuinely poor (radius ratio 3.2e-4, minimum dihedral
0.72 degrees) and every one of them is structurally valid.

**Can invalid RM-MESH-08 return NG_OK + zero Tet4 and PASS?**
The suite requires `generate()` to **fail** and requires that nothing was
published: `heldMeshCount() == 0`, `mesh()`, `map()` and `quality()` all null,
and `currency() == generation_failed`. A zero-element mesh is structurally
invalid anyway (`MeshIssueKind::EmptyMesh`), so it could not be published.
**Mutation M6 makes the body build: killed on 7.**

**Can reference tests use stale geometry from a prior generation?**
`requireMeshableGeometry` refuses stale geometry before anything else, and the
suite additionally asserts `currency() == current` and `isStale() == false`
after every generation.

**Can a geometry edit leave the old mesh current?**
Asserted on two models in the dedicated test (`StaleGeometry`, not merely
"stale"), and on all eight by the runner, whose exit code depends on it.

**Can a settings change leave the old mesh current?**
Asserted for the global target (`SetGlobalMeshSizeCommand`) and for the local
size (`EditLocalMeshSizingCommand`), both reporting `StaleIntent` — the
distinction a boolean could not express.

**Can save/load restore generated Tets instead of regenerating?**
Checked three ways: the file **bytes** contain no `"nodes"`, `"tetrahedra"` or
`"elements"` key; the files are 5-6 kB where a mesh would be tens of kB of
coordinates; and after loading, `heldMeshCount() == 0` and
`currency() == no_mesh` until something asks for a mesh.

**Can save/load change the GeometryReference target?**
The whole `MeshControlDefinition` compares equal across the round trip, and the
suite additionally compares the local control's `FaceName` list and each
boundary set's id, name and faces one by one.

**Can CLI results differ from core?**
The CMake regexes carry the exact node, element and facet counts and the exact
SI volume doubles that `MeshModelsTests.cpp` measures in process for the same
models. A CLI substituting its own default, sizing or validation would print
something else.

**Can a CLI failure return exit 0?**
RM-MESH-08's five queries each assert `EXIT_CODE 1` and a `STDERR_REGEX`; none
prints to stdout.

**Can one reference test depend on another test's files or state?**
Every `MeshedReference` builds its own document in its own constructor. The
persistence test writes to its own `TempDir`. The CLI fixtures read one
directory the runner writes, and the runner is a ctest `FIXTURES_SETUP` — which
ctest pulls in even when the run is filtered, whereas a `DEPENDS` on an excluded
test is dropped. Every reader is a read-only query, so they are safe in parallel
and survive `--repeat`.

**Can a filter execute zero reference tests?**
`ZeroMatchGuard.cmake` asserts a discovered **count** per filter, and proves its
own counter by showing that an impossible filter discovers 0 and that
`ctest -R` on it **exits 0**. This milestone's two filters were added, with
minimums of 20 and 15 against actual counts of 28 and 25.

**Can one model be silently skipped?**
The execution gate iterates the catalog and counts: 8 distinct IDs, 8 meshed, 1
refused, 9 rows, plus a loop requiring each of `RM-MESH-01`..`08` to be present.
**Mutation M7 drops a model: killed on 7.**

**Can quality expected values be copied from production output?**
No quality value is an expected value. The gates are `invalid == 0`,
`satisfiesPolicy()`, and **comparisons** — the thin plate against the block, the
placed body against the base. The metric definitions are P16-QUALITY-001's
qualified contract and are deliberately reused rather than restated, which the
support header says in as many words.

**Can tolerance be so loose every mesh passes?**
Every tolerance is recorded with its measurement beside it: 1e-9 against a
measured 2.450e-16, 1e-12 against 4.941e-16, 1e-4 against 6.0e-6. The curved models
get a derived bound rather than a tolerance, and the bound is 29.32 times
narrower than the defect it exists to catch. **Mutation M9 makes a model 0.7%
too long: killed** — by one assertion, the declared-volume cross-check, and the
mutation README records why that is the only gate that can catch it and why it
is sufficient.

**Can Debug and Release have different mapping or quality classifications?**
Measured: all three presets give 28 cases and the same assertion count, and the
CLI fixtures' exact counts and volume doubles pass under `-O0 -g`, under the
optimiser and across a DLL boundary.

**Can generated evidence be stale relative to the final source tree?**
Every figure in `RESULTS.md` is printed by the suite or the runner, and the
runner's output is kept in `qualification/`. The freeze records the eight-path
tree fingerprint, and the post-run fingerprint is compared with it; `docs/` is
outside the fingerprint by construction, so the evidence may be written after
the freeze.

**Can the final reference suite be run against a different tree from the
committed tree?**
`qualify.cmd` records the fingerprint at both ends of the run from a scratch git
index, and the staged tree is compared with it before the commit. See
`FREEZE.md`.

## Beyond the brief's list

Four more attacks, and what they found.

**Could the suite pass with the hole in the wrong place?** The occupancy check
is anchored at (50, 30) — a literal in the test, not read from the model. A hole
built elsewhere would put nodes inside the tested disc. The suite also asserts
`closestNode < radius`, so a hole that had moved **away** from the tested region
would make the check vacuous and fail.

**Could a boundary set resolve to the wrong facets and still pass?** For
RM-MESH-02 and RM-MESH-04 the sets' facet counts must **sum exactly** to the
boundary (34+34+72 = 140, 72x4 = 288) and be pairwise disjoint, so facets cannot
be double-counted or missed.

**Could the enclosed-volume check pass on an inward-facing boundary?** It is
computed from the stored winding with no absolute value, so an inward-facing
surface gives a negative volume and `sound()` is false. The hole-wall normal
check depends on this and would also invert.

**Could `MeshedReference` hide a regeneration failure?** Its constructor calls
`regenerateAll` and requires the call to succeed but **not** the report — which
is deliberate, because RM-MESH-08's body is meant to fail. The failure case
asserts `succeeded() == false` explicitly, and the suite-wide gate asserts
`report.succeeded() == info.expectMesh`, so a model that silently stopped
building would be caught rather than quietly skipped.

## Result

```text
QUESTIONS:           25 from the brief, plus 4 of the reviewer's own
FINDINGS:            5
PRODUCTION DEFECTS:  2  (F1, F2) -- fixed
SUITE DEFECTS/GAPS:  3  (F3, F4, F5) -- fixed
MUTATIONS:           9  -- 8 killed, 1 proven-equivalent survivor
REMAINING:           one test-coverage gap in P16-VOL-001's own file, named in
                     the mutation README and in KNOWN LIMITATIONS, not fixed
                     because it is outside this milestone's authorised subject
VERDICT:             PASS
```
