# P16-SURF-001 — adversarial review

Every attack the brief lists, answered against the implementation. An attack answered by a test
names it; one answered by construction says what makes it impossible; one answered by a mutation
says what broke.

```text
ATTACKS              22 from the brief, plus 4 raised here
FINDINGS             5
PRODUCTION DEFECTS   0
FIXED                5 -- all in this milestone's own tests
MUTATIONS            3 applied, 3 caught
```

All five findings were in my own test code, and three of them were arithmetic or API mistakes
that a looser assertion would have hidden. The most useful one is F1, because the lazy fix was
sitting in the failure message.

---

## Findings

### F1 — A determinism test that compared identity instead of content

**Attack.** Is repeated generation deterministic?

The test said `againBox.mesh == firstBox.mesh` and failed on all three fixtures. The code was
right: `Mesh::operator==` includes the `MeshStamp`, and a `MeshId` is **unique per mesh by
design** (ADR-031, so that a handle from one mesh is refused against another). Two independently
generated meshes of one body are therefore deliberately unequal however identical their content.

I had documented exactly this in P16-DATA-001's own README — *"two meshes that discretise the
same body identically but were built separately have different MeshIds and so compare
unequal"* — and then used `==` anyway.

**Resolution.** Determinism now compares content through a helper: node handles, exact
positions, oriented connectivity, element handles, regions and bounds. And a new test,
`SurfaceMesh_TwoMeshesOfTheSameBodyAreNeverTheSameMesh`, pins *why* `==` is the wrong tool, so
the next reader does not repeat it.

### F2 — A translation invariance test that translated by 137 metres

**Attack.** Is the enclosed-volume relation translation-invariant, as it must be for a surface
that genuinely closes?

It is, and the test still failed: `Length::fromSi` takes metres and I meant millimetres, so a
50 mm body was moved 137 m. Each triangle's contribution became ~4e5 m³ while their sum is
3e-5 m³ — a cancellation ratio near 1e10, consuming about ten significant digits. The measured
drift was 3.7e-7 relative, against my 1e-9.

**Resolution.** Both cases, because the extreme one is informative. A 137 **mm** translation at
1e-9, which is the invariant; and a separate 137 **m** test whose 1e-5 tolerance is derived from
the cancellation rather than chosen to pass. The area, which involves no cancellation, is held
exact in both. A drift there would mean a crack, and the test now says which it is.

### F3 — A capped-cavity guard that no correct implementation could pass

**Attack.** Would the tube fixture notice if the cavity were capped or its inner wall missing?

My guard was `enclosedVolume < 0.5 × outerCylinder`. A tube of Ro=20, Ri=12 is
(400−144)/400 = **64%** of its outer cylinder, so the assertion was arithmetically impossible.
It would have failed forever and been "fixed" by loosening it, which is how a real
discriminator gets quietly removed.

**Resolution.** The discriminating ratio is now asserted explicitly — `exact < 0.65 ×
outerOnly` — alongside a bound the true value satisfies. The capped answer is 1.56x the true
one, so the fixture does discriminate, and the arithmetic is on the page instead of in my head.

### F4 — Missing `using` declarations, and an unused helper

Routine, recorded for completeness: the identity types live in `bettercad::meshing` and the test
imported only some of them; and `SurfaceMesh.cpp` carried an `edge()` helper made dead by using
`triangleArea` for areas. Both caught by `-Werror` before any test ran.

### F5 — What the boundary-edge mutation does NOT protect

**Attack.** Does the suite protect the boundary-edge counter?

Mutation M3 disabled it and **only one test failed**: the deliberately-open patch fixture. I had
expected the watertightness assertions on the real solids to fail too. They did not, and the
reason is worth stating rather than glossing: those solids **are** closed, so their boundary-edge
count was already zero and removing the counter changes nothing about them.

**Consequence, recorded not fixed.** The closed fixtures cannot protect that counter. Only
`ValidateSurface_CountsBoundaryEdgesOfAnOpenPatch` does. That is adequate — one test that fails
is proof — but it is one test, and anyone deleting it as "redundant" would silently remove the
only guard on the watertightness claim.

---

## The brief's attacks, answered

**Can display triangulation be mistaken for the engineering mesh?** They are different types:
`geometry::Mesh` is a vertex-per-face soup with raw indices, `EngineeringSurfaceMesh` wraps a
P16-DATA `Mesh` with identified nodes plus a validation verdict. Neither converts to the other,
and `geometry::Mesh`'s own doc comment now states that its per-face duplication is the point of
the type and its limit.

**Can viewer deflection settings alter engineering results?** No, by construction:
`triangulate()` meshes a `BRepBuilderAPI_Copy` with `copyMesh=false`, so no cached triangulation
is read. `SurfaceMesh_IsUnaffectedByAnEarlierCoarseTriangulationOfTheSameBody` runs a coarse pass
first, then an engineering request with controls 100x finer, and requires the engineering result
to follow the engineering controls — then runs another coarse pass afterwards and requires the
engineering result to be unchanged.

**Can stale cached `Poly_Triangulation` be reused under the wrong controls?** No. `copyMesh=false`
means the copy begins with no triangulation at all, so the mesher must produce one from the
controls it was given.

**Can stale geometry be triangulated after a failed regeneration?** No —
`SurfaceMesh_RefusesStaleGeometryBeforeTriangulatingAnything` and
`..._RefusesABlockedFeatureWithNoCurrentBody`. **Mutation-proved**: disabling the currency check
two layers down in `GeometryPreparation` fails exactly the stale test, which is what shows the
chain is wired rather than bypassed.

**Can `TopLoc_Location` be ignored?** There is one place a node becomes a model-space point and
it applies the transform there. Nothing reads `Node(i)` without it.

**Can `TopLoc_Location` be applied twice?** Same reason — one application site, no second pass
over the vertices.

**Can REVERSED CAD faces produce inward triangle winding?** Not while the swap is there, and the
suite depends on it heavily: **ignoring `TopAbs_REVERSED` fails 23 of 30 tests**, including the
box's watertightness, its Euler characteristic, both area checks and the enclosed-volume
agreement. A box has reversed faces, so this is the main line rather than a corner.

**Can NodeIds be unified by coordinate equality in an order-dependent way?** The unification is
**exact**, so there is no clustering and no greedy merge order to depend on — the A≈B, B≈C,
A≉C hazard §53 describes cannot arise without a tolerance. Numbering is then assigned in
ascending coordinate order, so it does not depend on face traversal either;
`SurfaceMesh_NodeHandlesFollowCoordinateOrderNotFaceTraversalOrder` checks the positions come
back sorted.

**Can two adjacent faces leave a crack?** If they did, the boundary-edge count would be non-zero
and `generateSurfaceMesh` would **refuse**. That is the safety property: a welding failure cannot
be reported as watertight. The cylinder is the live case — a periodic face whose seam must fuse —
and it closes.

**Can the boundary-edge count be zero while non-manifold edges exist?** `watertight()` requires
all three counts, and `ValidateSurface_CountsANonManifoldEdgeSharedByThreeTriangles` builds a
fixture with no boundary edges and one non-manifold edge, then requires `watertight()` to be
false.

**Can an inward-oriented closed surface be called valid?** No, and this is the check that edge
counting cannot make. `generateSurfaceMesh` requires the enclosed volume to be positive.
`EnclosedVolume_IsNegativeForAnInwardOrientedClosedSurface` takes the box's surface, reverses
every winding, confirms the result is **still watertight and coherent**, and confirms the volume
comes out exactly negated.

**Can hole walls disappear?** `SurfTube_HasTrianglesOnTheInnerWallSoTheCavityIsRepresented`
counts nodes at the inner radius, and the enclosed volume would be the outer cylinder's — 1.56x
larger — if the wall were missing.

**Can a hole be capped?** Same two checks, plus an assertion that no node sits on the axis.

**Can inner hollow-tube wall normals point into the material?** This is the sharpest orientation
test here, because "outward" means out of the **material**, not away from the origin, and on a
tube's inner wall those are opposite directions.
`SurfTube_InnerWallNormalsPointTowardTheAxisBecauseThatIsOutOfTheMaterial` requires the radial
component to be strongly **negative** inside and strongly positive outside, and counts both so
neither set can be empty.

**Can sorting connectivity for determinism destroy orientation?** The sort key is the triangle's
**node set**; the stored connectivity is the original oriented tuple. §52's separation, and the
box's six distinct outward normals would collapse if it were violated.

**Can zero-area triangles survive because NodeIds are distinct?** No — degeneracy is measured
from the geometry, and `ValidateSurface_CountsDegenerateAndDuplicateTriangles` uses three
distinct but collinear nodes.

**Can reversed duplicate triangles survive?** No. Duplicates are detected on the **sorted node
set**, so `[A,C,B]` is the same topological triangle as `[A,B,C]`; the same test asserts it.

**Can graphics smoothed normals be reused as engineering normals?** There are none in the
repository, and the engineering mesh stores no normal field at all — a normal is derived from the
stored winding when something needs one, so it cannot go stale against the positions it came
from.

**Can generating a surface mutate the canonical geometry revision?**
`SurfaceMesh_GenerationLeavesTheDocumentAndTheBodyUntouched` generates three times and checks the
document revision, the body's topology, the geometry revision, the volume and the CAD surface
area are all unchanged.

**Can a valid solid produce an empty "successful" surface?** No — an empty node or triangle list
is an explicit `EmptySurface` failure before validation even runs.

**Can unsupported disconnected geometry be silently meshed anyway?** Multiple solids are
**supported** by the qualified architecture (ADR-032 gives a mesh one region per solid), so there
is nothing to refuse. Each component must still close, which edge incidence enforces over the
whole surface without needing to know which component a triangle belongs to.

**Can cross-preset face traversal change NodeId ordering?** It cannot reach the numbering at all:
NodeIds follow ascending coordinate order and triangle order follows the node-set key, so neither
is a function of traversal. The three-preset regression confirms it rather than being the only
evidence for it.

---

## Attacks raised here

**Does a periodic face's seam actually fuse?** This was the main technical risk in choosing exact
welding over OCCT's `PolygonOnTriangulation`. A cylinder's side face has a closing edge whose two
sides carry geometrically identical nodes; if they did not fuse, the seam would stay open. The
cylinder and tube fixtures are watertight, so it fuses — and had it not, the output gate would
have refused the mesh rather than reporting a crack as closed.

**Does the curved-surface area converge from the right side?** A triangulated curved surface
**understates** the true area, because chords cut inside the arc. The cylinder fixture requires
coarse < fine < exact and the fine mesh within 0.1% of the closed form, rather than asserting an
equality that would be wrong in a direction nobody checked.

**Is the enclosed volume a real orientation test or just a volume check?** Both, and the two are
separable: the inward-surface test shows the sign detects global inversion, and the translation
test shows the magnitude detects non-closure. Neither alone would.

**Can the engineering mesh and STL export disagree about the same body?** They share the
triangulator, so a change to it affects both. The STL suite was run alongside this milestone's
tests — 120 tests covering the surface mesh, P16-DATA, P16-GEOM, STL export and the architecture
checks — and passes.

---

## What this review could not establish

**`TriangulationFailed` has no fixture.** No shape was found that the kernel accepts as a valid
solid and then fails to triangulate. The branch returns a structured failure carrying the
kernel's own message, and it is kept on the grounds P15-MASS-001 states for its own unreachable
guard — but it is untested, and recorded as such rather than implied to be covered.

**There is no display path to measure against.** The repository has no renderer and no
`AIS_Shape`, so the display/engineering separation is established by construction — no shared
mutable state — rather than by observing two paths diverge. When a display path arrives it will
need its own test that the two remain independent; what exists now is the structural guarantee
that makes such a test pass, not the test.

**Sphere coverage was not added.** The cylinder and the tube cover curved and periodic faces, and
§29 makes the sphere optional. A sphere would additionally exercise a degenerate pole edge, which
is the one geometry where exact welding has an untested edge case — recorded as a known
limitation rather than left implicit.
