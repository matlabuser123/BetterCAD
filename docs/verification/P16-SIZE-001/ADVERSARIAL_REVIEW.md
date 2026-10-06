# P16-SIZE-001 — adversarial review

```text
SUBJECT:  the final diff adding canonical mesh sizing
METHOD:   attempt to disprove "sizing intent is canonical, unit-safe,
          deterministic and actually reaches the mesh", by attacking the
          evidence rather than re-reading the code approvingly
DEFECTS FOUND AND FIXED DURING REVIEW:  4
CARRIED FORWARD:                        4
```

Every one of the four defects below was found by **measurement contradicting
an expectation**, not by reading the code. Three of them would have shipped a
sizing control that looked plausible and did nothing.

## Defects this review found, and fixed

### 1. The global target did nothing at all

The first implementation set `Ng_Meshing_Parameters::maxh` — the parameter
documented as "Maximum global mesh size allowed". A 40 mm block meshed at 20,
10 and 5 mm gave the **identical** 9-node, 12-tetrahedron mesh three times.

The volume mesher asks `Mesh::GetH(p) = min(hglob, localh(p))`, and only
`Ng_RestrictMeshSizeGlobal` writes `hglob`. Fixed by routing the global target
through it. A three-row coarse/medium/fine test now pins the trend.

**Why it is worth recording:** the parameter's name is a correct description of
its intent and a misleading description of its effect. The only way to know was
to measure and then read the source.

### 2. A point restriction left the target region coarser

Restricting at each of a face's surface nodes constrains an infinitesimally
thin sheet. Measured on a cylinder's top disc, with the restriction points
verified to land at z = 20 exactly: the region went from 50 nodes to 40 and
from mean edge 4.68 mm to 5.85 mm — **coarser than with no control at all**.

Fixed by making a face a **slab** and using `Ng_RestrictMeshSizeBox`.

### 3. The slab refined one face and not the opposite one

With a slab centred on the face and one target deep, the top disc refined (69
nodes against 43) and the bottom disc did not — it produced **490
tetrahedra, fewer than the 554 with no control at all**.

`Ng_RestrictMeshSizeBox` walks from the box's minimum corner in steps of `h`,
so the top slab sampled at 18.5 mm (inside a body running to 20) while the
bottom slab sampled at −1.5 mm (outside) and 0.0 mm (the surface only).

Fixed by extending the slab **inward along the face normal**, three target
sizes deep. Both directions then work symmetrically: 72 against 59 for the
top, 71 against 52 for the bottom.

**This is the defect the wrong-region test exists to catch**, and it caught
it. Had the suite only refined one face, the milestone would have passed with
local sizing working in one direction.

### 4. Having a local control changed the whole mesh's grading

`Mesh::RestrictLocalH` creates the size tree when none exists, with a
**hardcoded grading of 0.8**, bypassing `mparam.grading` (0.3).
`Ng_GenerateVolumeMesh`'s `CalcLocalH` then builds the tree only if one is
absent — and a restriction has already built it. So the grading depended on
whether a local control happened to exist.

Fixed by pinning `grading = 0.8`, the value the restriction path forces, so
both paths agree. Sizing intent must not depend on whether another control
exists.

## The attack questions

```text
Q  Can a target size be a unitless double?
A  No. globalTargetSize is std::optional<Length> and LocalMeshSizing::target-
   Size is Length. There is no double-taking overload, and the seam carries
   Length too -- the conversion to a bare double happens inside the adapter,
   one line before the nglib call.

Q  Can 10 mm be interpreted as 10 m?
A  No, and it is tested two ways. Length is dimensioned and SI internally, so
   10_mm and Length::fromSi(0.01) compare EQUAL as intent; and a 4 mm body at
   a 1 mm target against a 40 mm body at a 10 mm target must give median edges
   differing by roughly ten, which a factor-of-1000 slip would miss by orders
   of magnitude.

Q  Can zero, negative or non-finite sizing reach Netgen?
A  No. resolveSizing runs validate() first and returns InvalidArgument, so the
   backend is never called. Tested for 0, negative, NaN, +inf and -inf, on both
   the global target and a local one. Non-finite is checked BEFORE
   non-positive, because the comparison that decides "positive" answers false
   for NaN and would report it as non-positive -- true, but it hides that the
   value is not a number.

Q  Can the global target depend on a hidden Netgen default?
A  No. A request with no global target gets BetterCAD's own default -- the
   bounding-box diagonal -- and ResolvedSizing::globalIsDefault records that
   it did. All fifteen parameters nglib actually transfers are set
   explicitly; the seven it never transfers are documented as dead rather than
   set for show.

Q  Can upgrading Netgen alter semantics through an unpinned default?
A  Not through a transferred parameter: every one is assigned before
   generation. A change to nglib's own CODE -- a different formula in
   CalcLocalH, say -- would still change results, which is true of any
   dependency and is why the version is pinned by hash.

Q  Can local sizing use a NodeId, an ElementId or a viewer triangle index as
   its canonical target?
A  No. LocalMeshSizing holds a FaceName and nothing else. Those types do not
   convert to one another (P16-DATA-001's compile-fail cases), and FaceName is
   the persistent identity P12-STREF-001 qualified.

Q  Can a deleted CAD region silently rebind?
A  No. Resolution is geometry::findNamedFaces, which matches the NAME; a face
   that no longer carries it yields no faces and the control becomes
   Unresolved. There is no nearest-face fallback and no resolution by object
   name -- the rebinding defect P15 forbids for materials. Tested with a
   HoleBottom on a block that has no hole, and with a face of another object.

Q  Can two local controls produce insertion-order-dependent results?
A  No, by construction rather than by rule: every backend restriction is a
   maximum and RestrictLocalH keeps the smaller, and the minimum of a set does
   not depend on arrival order. The resolved restrictions are additionally
   held in a position-keyed std::map and the slabs sorted, so the list handed
   to the backend is identical. Tested with two insertion orders: equal
   restriction lists, equal node and element counts, equal volume.

Q  Can local refinement accidentally refine the whole body?
A  No -- tested as a named assertion, not assumed. With a 1.5 mm control on a
   cylinder's top disc and a 6 mm global target, the target region's mean edge
   is 3.68 mm while the mid-body is 9.51 mm, and the mid-body is no finer than
   it was WITHOUT the control. That last comparison is what separates
   refinement from a global size change in disguise.

Q  Can local refinement target the wrong face?
A  This is defect 3 above, and it did. Now tested as a cross-comparison: the
   top region is denser when the top is refined than when the bottom is, AND
   the bottom region is denser when the bottom is refined than when the top
   is. Both directions, so neither can pass by accident. The resolved slab's
   z range is asserted directly as well, so a mapping error shows up as a
   wrong coordinate rather than as a confusing density number.

Q  Can a fine global target increase element size because the mapping is
   backwards?
A  No. Three targets, each asserted finer than the last on median AND mean
   edge length, with the volume constant to 1e-9 across all three.

Q  Can a material edit invalidate the mesh unnecessarily?
A  No. Assigning a material and then changing its density leaves the sizing
   intent identical and the mesh current. Tested, because a false dependency
   wastes a remesh on every property edit.

Q  Can a sizing edit fail to invalidate the old mesh?
A  No. isStale(document, mesh, controls) compares the canonical intent by
   value: a changed global target or an added local control makes the mesh
   stale, and an EQUIVALENT request (20 mm expressed in metres) does not.

Q  Can viewer tessellation controls modify engineering sizing?
A  No. The boundary deflection and the volume sizing are different fields of
   different types in VolumeMeshControls, so a change to one is not a change
   to the other -- asserted both ways: the sizing intent is untouched, and the
   overall request still differs so a mesh built from the old one is stale.

Q  Can the adapter bypass P16-SURF to gain Netgen's OCC local-sizing features?
A  No. USE_OCC is OFF in deps/CMakeLists.txt, the OCC front end is not built,
   and the only nglib include in the tree is the one file rule 5 permits. The
   local mechanism used is the one the approved pipeline offers.

Q  Can an invalid one-of-N control be ignored while the mesh reports success?
A  No. One unresolved control refuses the whole mesh with SizingNotResolved.
   Meshing with the rest would produce a mesh that is not the one asked for.

Q  Can an extreme request be silently clamped?
A  Not by BetterCAD, and the backend's clamp is switched off: minh is pinned
   to 0 precisely because RestrictLocalH begins "if (hloc < hmin) hloc = hmin".
   A target coarser than the body is accepted and produces a valid mesh --
   validity matters, element count does not.

Q  Can NG_OK with zero tetrahedra still appear successful?
A  No. P16-VOL-001's guard is unchanged and still tested: the acceptance
   condition is the return code AND positive node and element counts AND every
   element being NG_TET.

Q  Can Debug and Release resolve different local precedence?
A  The canonical resolution uses no unordered container and no floating-point
   tie-break: the restrictions are a position-keyed std::map and the slabs are
   sorted on an exact tuple. All three presets run the full suite. Mesh
   determinism across presets remains unasserted, as P16-VOL-001 recorded.
```

## Carried forward

```text
1. Local sizing refines the VOLUME, not the boundary.
   The boundary is fixed and validated before the backend is called, so a
   face-local control makes the tetrahedra near a face smaller without
   re-triangulating it. Surface-local refinement needs either P16-MAP-001's
   facet attribution plus a surface-side control, or the Netgen OCC front end
   -- which ADR-033 forbids as a bypass of the validated boundary.

2. The slab is a bounding box, not the face.
   A face's region is the axis-aligned box of its nodes extended inward. For a
   face that is small relative to its bounding box -- an L-shaped face, or one
   face of a thin diagonal rib -- the slab reaches material the control did not
   name. Exact region targeting needs facet attribution (P16-MAP-001).

3. Coplanar faces facing the same way are not distinguished.
   Membership is "on this face's plane, facing its way". Two distinct coplanar
   faces with the same outward normal, only one of them named, would both be
   claimed. findFaces() treats them as the same plane too, so this is
   consistent with the rest of the repository rather than new, but it is a
   limit on what "this face" means here.

4. Only planar and cylindrical faces can become regions.
   Those are the kinds FaceInfo describes. A conical, spherical or toroidal
   face resolves and then reports Unsupported -- explicitly, and the mesh is
   refused rather than meshed without the control.
```

## Result

```text
RESULT:  PASS
         Four defects were found by this review, three of them sizing controls
         that would have shipped doing nothing or the opposite of what was
         asked. All four are fixed and re-verified. Four limitations are
         carried forward explicitly, none of which contradicts the claim as
         stated.
```
