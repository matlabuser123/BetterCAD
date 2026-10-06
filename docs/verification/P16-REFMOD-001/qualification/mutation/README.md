# P16-REFMOD-001 — mutation testing

```text
QUESTION: would this suite notice if a reference model were wrong?
METHOD:   apply one defect, rebuild debug-ext, run the relevant filter, record
          which assertions fired, restore
RESULT:   8 killed, 1 proven-equivalent survivor
```

A reference suite's own correctness cannot be argued; it has to be attacked.
Each mutation below is one of the defects the brief names, injected into the
committed artefact or into the production code it exercises.

| # | Mutation | Verdict |
| --- | --- | --- |
| M1 | RM-MESH-01 is a **cube**, 120 x 120 x 120 | killed, 4 |
| M2 | RM-MESH-03's **hole is filled** (the inner loop becomes construction) | killed, 11 |
| M3 | RM-MESH-04's **inner and outer wall references are swapped** | killed, 8 |
| M4 | RM-MESH-06's **rigid transform is not applied** | killed, 5 |
| M5 | the local **slab swallows the whole body** (production, `resolveSizing`) | killed, 8 |
| M6 | RM-MESH-08's **profile is closed**, so the body builds | killed, 7 |
| M7 | **RM-MESH-05 is dropped** from the catalog | killed, 7 |
| M8 | **`abs()` on the tetrahedral volume** (production, `tetrahedralVolume`) | **equivalent mutant, proven** |
| M9 | RM-MESH-01 is **0.7% too long** (120.84 mm, catalog unchanged) | killed, 1 |

The "match zero cases" mutation the brief lists is not injected, because
`tests/cli/ZeroMatchGuard.cmake` already proves it from the inside: it asserts a
discovered **count** per filter, demonstrates that a deliberately impossible
filter discovers 0, and demonstrates that `ctest -R` on that filter **exits 0**
— which is the whole premise. This milestone's two filters were added to its
table.

## What each kill tells us

**M1 — the cube.** Four assertions: the three asymmetry checks
(`a != b`, `b != c`, `a != c`), which the suite makes rather than trusting the
builder, and the declared-volume cross-check. A cube is the classic way for an
axis-permutation defect to hide, and the suite refuses to contain one.

**M2 — the filled hole. Three independent gates fired**, which is the point of
layering them:

```text
CHECK( bound.contains(meshed) )                     the analytic bound
CHECK_THAT( cadVolume, WithinRel(expected, 1e-12) ) OCCT against closed form
CHECK( occupancy.violations() == 0 )                independent occupancy
CHECK( occupancy.nodesInside == 0 )
CHECK( occupancy.centroidsInside == 0 )
CHECK( occupancy.closestNode >= hole.safeRadius() )
CHECK( report.cadFaceCount == expectedFaces )       the mapping
REQUIRE( resolved->fullyResolved() )                the hole_wall set
```

Any one of them alone would have caught it. The brief warns that volume
agreement is not sufficient because a topology error could compensate
elsewhere; here the occupancy check is shown to be load-bearing rather than
decorative.

**M3 — the swapped walls, and the lesson about which check matters.** The
pairwise facet-disjointness assertions **did not fire** — correctly, because the
facets are still disjoint, merely attributed to the wrong names. What caught it
was the node-radius check (every outer-wall node at Ro, every inner-wall node at
Ri) and the normal-direction check (outer faces away from the axis, inner faces
toward it). A suite that had checked only disjointness and counts would have
passed a mesh whose pressure load went on the wrong surface.

**M4 — the untransformed body.** Five assertions, including the two the
adversarial review added late: the bounding-box displacement, the boundary node
gap after `R x + t`, the interior node gap, and the **mapped facet normals**
against `R` applied to the base normal. The normal check fired twice, once per
facet.

**M5 — local sizing that refines everything.** This is the brief's own attack,
and the result is the clearest in the set: the "target region is refined"
assertions **did not fire**, because a global refinement does refine the target.
What caught it was the **mirror** — a third mesh refining the opposite face, so
the same band can be compared between the two — in all four of its directions,
plus the slab-position checks:

```text
CHECK( refinedAtF.median < mirroredAtF.median )     the mirror
CHECK( mirroredAtG.median < refinedAtG.median )
CHECK( refinedNodesF > mirroredNodesF )
CHECK( mirroredNodesG > refinedNodesG )
CHECK_THAT( slabLow.y, WithinAbs(0.0, 1e-3) )       the slab is AT F
CHECK( slabHigh.y < b / 2.0 )
CHECK_THAT( mirroredHigh.y, WithinAbs(b, 1e-3) )
CHECK( mirroredLow.y > b / 2.0 )
```

A qualification resting on "the element count rose" would have passed M5
outright. The brief forbids that criterion, and this is the measurement showing
why.

**M6 — RM-MESH-08 builds.** Seven assertions across the dedicated failure case,
the suite-wide execution gate and the determinism gate. Note what the suite
checks is stronger than the brief's forbidden outcome: not "a mesh with zero
tetrahedra was reported as success", but **nothing was published at all** — no
mesh, no map, no quality report, and `currency() == generation_failed`.

**M7 — a model silently dropped.** Seven assertions. The catalog's size, the
distinct-ID count, the per-ID presence loop, the executed count, the meshed
count and the row count all fire. "No silent omission" is enforced, not
promised.

**M9 — a wrong dimension, and the honest finding.** Killed, but by **exactly one
assertion**: the declared-volume cross-check.

```text
CHECK_THAT( derived, WithinRel(info.analyticVolumeMm3, 1e-15) )
```

The volume gates did **not** fire, and that is not a weakness to paper over —
it is structural. The suite derives its expected volume from the model's **own
parameters**, so when a parameter changes, the closed form, the kernel's CAD
volume and the mesh all move together and agree. The bounding-box check reads
the same parameter, so it agrees too.

**So the catalog's declared volume is the single defence against "the builder
builds the wrong size", and M9 is the measurement that proves it carries real
weight** rather than being documentation. It is checked for all nine documents.
The complementary defect — dimensions that preserve `abc` but swap two axes — is
caught by the per-axis bounding-box check instead, so between the two the
defence is complete. This is why the brief asks for the analytic volume in the
model metadata (§41) and why the suite cross-checks it rather than trusting it.

## M8 — the surviving mutant, with a proof rather than an excuse

```text
total += std::abs(signedVolume(...).si());     // instead of the signed value
```

It survived the whole `[refmod][mesh]` suite. It is **unobservable through any
`VolumeMesh` a caller can possess**, and the proof is in the call order of
`generateVolumeMesh`:

```text
src/meshing/VolumeMesh.cpp
  414   const MeshValidationReport report = validate(volume);
  418   return failure(VolumeMeshFailure::InvalidMesh, ...)   on any issue
  431   const Volume tetVolume = tetrahedralVolume(volume);
```

`validate` reports `DegenerateTetrahedron` for a zero signed volume and
`InvertedTetrahedron` for a negative one, and either refuses the mesh. So at
line 431 every term is strictly positive, and `abs(x) == x` over a set of
strictly positive numbers. `VolumeMesh`'s constructor is private with
`generateVolumeMesh` its only friend, so there is no other way to obtain one.

**The premise is measured, not assumed.** The suite's own orientation census
computes a determinant per element from the node coordinates and finds, across
all eight valid models, **1751 positive, 0 zero, 0 negative, 0 non-finite**.

**And the check that would distinguish them exists and is correct**:

```text
CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3),
           WithinRel(structure.orientation.totalVolume, 1e-12));
```

where `structure.orientation.totalVolume` is the suite's own **signed** sum. The
branch is unreachable, not untested — which is the same verdict P16-CLI-001's M5
reached about `mesh-validate`'s gate, and for the same structural reason.

> **A coverage gap this exposed, named and left where it belongs.** The free
> function `tetrahedralVolume(const Mesh&)` is also callable on a hand-built
> `Mesh`, and its header promises that "an inverted element must drag the total
> down". No test in this repository builds such a mesh and checks that promise,
> so M8 survives the whole suite and not only the reference models. That is a
> test-coverage gap in **P16-VOL-001**, whose file owns the function and its
> contract; the code is correct, and closing the gap is one small unit test
> there rather than a reference model here. It is recorded in this milestone's
> known limitations and not fixed, because P16-REFMOD-001's authorised subject
> is the reference suite.

## A process defect in this harness, recorded

**M5 was left applied after its run.** The harness restores a fixed set of
pristine files in an `EXIT` trap, and `src/meshing/MeshSizing.cpp` was not in
that set — M5 was the first mutation to touch it. The only reason it was caught
is that `MeshSizing.cpp` is **tracked**, so `git diff` showed the mutation
alongside the milestone's legitimate change to the same file.

A mutation left in one of this milestone's **untracked** new files — which is
most of them — would have been invisible to `git status` and `git diff` alike,
and would have been frozen into the qualification. The same failure mode is in
this repository's working notes from P16-CLI-001, from the other direction: an
orphaned harness left a mutation applied to an untracked source file.

Two changes followed: `MeshSizing.cpp` joined the pristine set, and every file
is now diffed against the snapshots after each run rather than trusted to the
trap.

```text
AFTER THE LAST RUN, all six mutated files byte-identical to their snapshots
  MeshModels.cpp           identical
  MeshReferenceModels.hpp  identical
  MeshModelsTests.cpp      identical
  MeshTestSupport.hpp      identical
  VolumeMesh.cpp           identical
  MeshSizing.cpp           identical
  no mutation marker anywhere in the tree
```

Four of the six were edited again after the mutation runs — the test files for
the cost reductions and the quality matrix, and the two model files for a
corrected comment and a reflowed line — so the verification was repeated at the
end, line by line, and the only differences are those intended edits.

**The snapshots themselves are not committed.** They were a working safety net
while most of these files were still untracked, and once the milestone is
committed the authoritative snapshot is git: `git diff` and `git show` give the
same comparison without 230 kB of duplicated source sitting in an evidence
directory, where it could drift from the real thing and mislead a later reader.
The harness and the pristine set are described above precisely enough to rebuild
either.
