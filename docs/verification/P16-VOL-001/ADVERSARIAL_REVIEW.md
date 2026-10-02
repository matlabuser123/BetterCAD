# P16-VOL-001 — adversarial review

```text
SUBJECT:  the final diff implementing Tet4 volume meshing
METHOD:   attempt to disprove "this mesh is correct and cannot be misused",
          by attacking the evidence rather than re-reading the code approvingly
DEFECTS FOUND AND FIXED DURING REVIEW:  4
CARRIED FORWARD:                        4
```

## Defects this review found, and fixed

### 1. Every tetrahedron Netgen returns is inverted

The first end-to-end run refused every mesh:

```text
volume mesh refused: the generated mesh is not data-valid
                     (12 issue(s), first: inverted_tetrahedron)
```

Netgen orders a tetrahedron's nodes so that `det(p1-p0, p2-p0, p3-p0)` is
**negative** — the opposite of BetterCAD's convention. A 20x30x40 box came back
as 12 tetrahedra, 12 of them negative.

This is the defect the review existed to catch, and the interesting part is the
three ways of "fixing" it that are wrong:

```text
abs() in signedVolume            destroys the only evidence that a generator
                                 produced an inverted element
flip the comparison              makes BetterCAD's convention depend on which
                                 backend is loaded
reorder each element that comes   cannot distinguish "their convention" from
out negative                      "their bug", so it silently repairs a
                                  genuinely inverted element
```

Fixed by a **fixed odd permutation at the adapter** — one node swap, applied
unconditionally — which is representation translation, what an adapter is for
(ADR-033). An element Netgen gets *wrong* still arrives negative and is still
refused. A new test pins the uniformity the translation depends on, so a future
Netgen that changed convention fails loudly rather than producing a mesh of
uniformly inverted elements.

A single swap, because a swap is **odd**. Reversing all four nodes is
`(0 3)(1 2)` — even — and changes nothing while looking like a fix.

### 2. `isStale` could be asked about the wrong body

The first API was `isStale(document, feature, mesh)`. It answers confidently
against whatever feature it is handed, so passing the wrong one yields a
meaningless verdict with no way to tell — and the natural caller, holding a
mesh and a document, has to remember which feature the mesh came from.

Fixed by recording the source in the mesh and taking it from there:
`isStale(document, mesh)`. `generateVolumeMesh` now takes the
`MeshableGeometry` the surface came from rather than a loose volume, so the
source, the kernel volume, the revision and the solid count are guaranteed to
describe the same body instead of being assembled from four arguments.

### 3. The surface's own validation report was trusted

`SurfaceValidation` is a plain struct. A caller could zero every count and
present a three-triangle open patch as a perfect boundary, and the first
version believed it.

Fixed: `generateVolumeMesh` re-runs `validateSurface` over the triangles and
uses what it finds, and recomputes the enclosed volume the same way. A test now
presents a patch with a deliberately clean report and watches it be refused.

That is the difference between a gate and a label, and it is what makes
"a viewer tessellation cannot enter the solver path" structural rather than a
matter of which type name was used.

### 4. A test bound that no correct mesh could satisfy

The tube's "the void was not filled" guard was written
`actual < filledBore / 2`, which is **arithmetically impossible**: the annulus
is 64% of its outer cylinder. The test failed on correct code.

Fixed, with the arithmetic written out beside it. Recorded because this is the
**second** time the same mistake has been made on the same fixture, and a
memory note about the first did not prevent it.

## The attack questions

```text
Q  Can a stale mesh be used?
A  Not silently. volumeMeshFor refuses stale geometry before anything inspects
   it (P16-GEOM-001's currency check), and a mesh HELD across an edit answers
   isStale(document, mesh) truthfully, including after the feature is deleted.
   BUT: nothing FORCES a holder to ask. See Carried forward (1).

Q  Can a NodeId become a CAD identity?
A  No. No node and no element carries an ObjectId; the mesh's provenance is one
   ObjectId on the VolumeMesh wrapper, which is provenance and not a reference
   from mesh data into the document. P16-DATA-001's compile-fail cases already
   prove the handle types do not convert.

Q  Can an ElementId survive a remesh incorrectly?
A  No. MeshStamp, and Mesh::owns refuses a foreign stamp. Qualified in
   P16-DATA-001 and unchanged here.

Q  Can NG_OK hide a failure? Can zero tetrahedra pass?
A  No, and this is the one behaviour the seam exists for. The acceptance
   condition is the return code AND a positive node count AND a positive
   element count AND every element being NG_TET AND every index in range. A
   test feeds an open surface and requires the refusal.

Q  Can an invalid Tet4 reach a solver?
A  No. validate() must be clean before a VolumeMesh exists, and VolumeMesh's
   constructor is private with generateVolumeMesh as its only friend. Three
   compile-fail cases prove it cannot be default-constructed, cannot be
   promoted from a plain Mesh, and exposes no mutation.

Q  Can an open surface create a volume?
A  No, by two independent routes: the re-validated surface check refuses it
   before the backend is called, and the backend's zero-element guard refuses
   it if it ever got that far.

Q  Can a void become filled?
A  No. Tested on a tube: no element centroid lies inside the bore, and the
   volume matches pi(Ro^2-Ri^2)h rather than pi Ro^2 h, which is 1.56x larger.
   Checked on CENTROIDS rather than nodes, because nodes legitimately sit on
   the inner wall.

Q  Can a viewer tessellation enter the solver path?
A  No. The entry point takes an EngineeringSurfaceMesh -- which generateSurfaceMesh
   alone can produce -- AND re-validates its triangles rather than trusting its
   report. Defect 3 above was exactly this hole, and it is closed.

Q  Can a Netgen header leak into core?
A  No. nglib.h appears in exactly one file under src/meshing/netgen/, and
   CheckLayering rule 5 fails the build on any other. Two fixtures prove the
   rule fires, one of them added in INFRA-NETGEN-001 for the generated headers
   the original rule missed.

Q  Can a configuration change leave a stale mesh?
A  No. requireMeshableGeometry refuses outright while an override is active
   (ADR-030's guard, reused unchanged), and the GeometryRevision mixes the
   active configuration in, so a switch moves it.

Q  Can persistence restore an invalid mesh?
A  Nothing derived is persisted, and this milestone added no io code. Checked
   rather than asserted: a document is saved AFTER meshing and the file is
   searched for mesh tokens, the way P15-PERSIST-001 checked the same property
   for derived mass.

Q  Is the conformity check capable of failing?
A  It is counted in both directions, so a backend that refined the boundary
   would show unmatched surface triangles and one that invented faces would
   show unmatched boundary faces. The extractor itself is tested where the
   answer is known by counting -- 4 faces for one tetrahedron, 6 for two
   sharing a face, not 8 -- and its outward windings are checked by requiring
   them to enclose the tetrahedron's own volume.

Q  Is the volume tolerance doing real work, or hiding a disagreement?
A  1e-9 relative, against the boundary's own enclosed volume. Both sides are
   sums of the same determinants over the same vertices, differing only in
   grouping, so the expected disagreement is near 1e-13. A geometrically wrong
   mesh misses by percent. The CAD volume is deliberately NOT the gate,
   because a faceted boundary understates it for curved faces.

Q  Was any gate weakened, any tolerance moved, any test skipped?
A  No. One tolerance was introduced and justified; no existing tolerance moved.
   validate() was made STRICTER by a new issue kind. The three guarded test
   files compile away entirely without a backend, and the qualification records
   that the backend WAS present and the tests DID run.

Q  Does Debug differ from Release, or static from shared?
A  All three production presets were built and tested; see README.md.

Q  Is the determinism claim as strong as it sounds?
A  Within a preset, yes: five runs at the seam compared on exact connectivity
   and node positions, and five end-to-end runs compared on handles, positions
   and connectivity in order. Across presets it is NOT asserted. See Carried
   forward (2).
```

## Carried forward

```text
1. Nothing forces a holder to check isStale.
   A VolumeMesh records its source and revision and answers truthfully, but a
   caller that never asks can read a mesh of geometry that has moved. ADR-030's
   answer is that a mesh is recomputed rather than restored, and the request
   path does refuse stale geometry -- so this bites only code that CACHES a
   VolumeMesh. P17's solver entry point should take the document and the
   feature, or re-check, rather than a bare mesh. Recorded as the design
   constraint it is, not as a defect in this milestone.

2. Cross-preset determinism is not asserted.
   Each preset runs its own tests, so a mesh that differed between Debug and
   Release would not be caught. USE_NATIVE_ARCH=OFF and a fixed parameter set
   are good reasons to expect agreement; they are not evidence of it. Comparing
   across presets needs an artefact to compare, and nothing exports a mesh yet
   -- so this waits for P16-CLI-001 or P16-VIZ-001.

3. Concurrent meshing is not exercised.
   Every call into the backend is serialised by a mutex because nglib keeps
   global state. No test runs two threads through it.

4. Multiple solids are refused rather than supported.
   ADR-032's one-region-per-solid remains the eventual design. The refusal is
   explicit, tested, and documented in ARCHITECTURE.md; it is a scope decision
   and not an oversight.
```

## Result

```text
RESULT:  PASS
         Four defects were found by this review -- one of them a genuine
         correctness defect that would have made every mesh invalid -- and all
         four are fixed and re-verified. Four limitations are carried forward
         explicitly, none of which contradicts the claim as stated.
```
