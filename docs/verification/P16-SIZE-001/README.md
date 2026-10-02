# P16-SIZE-001 — Global / Local Mesh Sizing

```text
STATUS:   PASS
TASK:     P16-SIZE-001 -- canonical, unit-safe, deterministic mesh sizing
PHASE:    P16 -- Meshing
DATE:     2026-10-02
```

BetterCAD owns its mesh sizing semantics. A global target and face-local
refinements are expressed as dimensioned `Length` values against persistent
`FaceName` references, resolved and validated before the backend is called,
and translated to Netgen in one file.

```text
MeshSizingControls -> ResolvedSizing -> VolumeBackendRequest -> nglib
   canonical           BetterCAD types      no backend type      one file
```

## Documents

```text
AUDIT.md              the Netgen sizing control surface and its hidden
                      defaults, read from the pinned SOURCE. Four findings.
BACKEND_MAPPING.md    exactly how canonical intent becomes Netgen calls
SIZING_RESULTS.md     what the controls measurably did
ADVERSARIAL_REVIEW.md 4 defects found by the review, 4 limitations carried
qualification/        the logs
```

## Baseline

```text
branch        main
HEAD at start e9fd82c  BetterCAD: implement Tet4 volume meshing foundation
tree          14660e093807abd3cac53f5ce5b5bc4ae3025337
origin/main   e9fd82c  (HEAD == origin/main)
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
backend       Netgen 6.2.2604, reported by the library itself
build root    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive)
```

## Prerequisites

Verified from the committed evidence, not assumed:

```text
P16-ARCH-001      25/25 ticked, RESULT PASS      evidence at 9964f88
P16-DATA-001      26/26 ticked, RESULT PASS      evidence at 20b04b9
P16-GEOM-001      20/20 ticked, RESULT PASS      evidence at 9c755e8
P16-SURF-001      21/21 ticked, RESULT PASS      evidence at 4005815
P16-VOL-001       19/19 ticked, RESULT PASS      evidence at e9fd82c
INFRA-NETGEN-001  RESULT PASS                    evidence at eaf7da4
```

`P16-VOL-001` was checked specifically rather than inferred from the backend
being qualified: 19/19, `RESULT: PASS`, 20 committed qualification logs, and
`0 stage(s) failed`. The six `TODO` strings in its evidence are references to
`TODO.md`, not unfilled placeholders.

## Scope

Implemented: the canonical sizing model, its validation, resolution against
geometry, the backend translation, and the audit that decided what the model
contains.

Not implemented, deliberately: commands and undo (`P16-CMD-001`),
serialisation (`P16-PERSIST-001`), CLI verbs (`P16-CLI-001`). The controls are
value-semantic, comparable and free of pointers, backend tags and mesh-local
ids precisely so that those milestones need not change this type.

## Netgen sizing audit

Four findings, each of which would have made sizing silently wrong. Full
detail with source citations in [AUDIT.md](AUDIT.md).

```text
1  maxh IS NOT THE GLOBAL SIZE CONTROL. Mesh::GetH returns
   min(hglob, localh(p)), and only Ng_RestrictMeshSizeGlobal writes hglob.
   Measured: a 40 mm block at maxh 20, 10 and 5 mm gave the IDENTICAL
   12-tetrahedron mesh three times.

2  A POINT RESTRICTION CANNOT REFINE A FACE. A face's nodes lie on a surface,
   so restricting at each constrains a sheet the mesher barely samples.
   Measured: 36 restrictions on a cylinder's top disc left the region COARSER
   than with no control at all.

3  THE SLAB MUST EXTEND INWARD, SEVERAL ELEMENTS DEEP.
   Ng_RestrictMeshSizeBox walks from the box's minimum corner in steps of h.
   Measured: a centred one-deep slab refined a top face and left the opposite
   bottom face with FEWER elements than no control at all.

4  RestrictLocalH HARDCODES grading 0.8 when it lazily creates the size tree,
   bypassing mparam.grading, and begins "if (hloc < hmin) hloc = hmin" --
   a silent clamp. So the grading depended on whether a local control existed.
```

### Hidden-default audit

`Transfer_Parameters()` is the only route from `Ng_Meshing_Parameters` into
the mesher. It copies **fourteen** fields; all fourteen are now set explicitly
before generation, so no BetterCAD semantics rest on a Netgen default.

**Seven fields the header declares are never transferred** —
`fineness`, `closeedgeenable`, `closeedgefact`, `minedgelenenable`,
`minedgelen`, `optsurfmeshenable`, `optvolmeshenable` — and are therefore dead
on this pathway. `fineness` and `optvolmeshenable` in particular read as
though they would matter. Setting them would be theatre; the audit records why
instead.

Two values BetterCAD deliberately differs on: `grading = 0.8` (finding 4) and
the global target routed through `Ng_RestrictMeshSizeGlobal` (finding 1).

## Canonical control model

```text
MeshSizingControls {
    std::optional<Length> globalTargetSize;   // nullopt -> BetterCAD default
    std::vector<LocalMeshSizing> local;       // { FaceName face; Length size; }
}
```

Value-semantic, comparable, with no pointer, backend tag or mesh-local id.

### Global target size

An **upper bound the mesher aims at**, not a promise that every tetrahedron
edge equals it: the mesher also respects the boundary discretisation, which
can make elements near a finely triangulated surface smaller than asked.
Calling it a *target* rather than a *size* is deliberate.

Maps to `Ng_RestrictMeshSizeGlobal` (and `maxh`, which participates in two
secondary places). See [BACKEND_MAPPING.md](BACKEND_MAPPING.md).

### Minimum size — N/A BY DECISION

Not exposed, and not merely deferred. Netgen's `minh` is real and transferred,
but its only observable effect through this backend is the silent clamp in
`RestrictLocalH`: a canonical minimum size would be a control whose purpose is
to make another control lie about what it was given. Pinned to **0**, which
disables the clamp.

### Maximum size — N/A

The global target **is** the maximum. A second control named "maximum" beside
it would be the duplicate with unclear interaction the brief warns against.

### Growth rate — DEFERRED, pinned

`grading` is real and transferred, and it materially affects how far a
refinement spreads. It is not exposed because its engineering meaning
("0 uniform ... 1 aggressive local grading") is not statable as a quantity
yet, and because the restriction path forces 0.8 regardless of what is asked.
Pinned to 0.8 and documented as a BetterCAD-fixed backend parameter.

### Curvature control — ALREADY EXISTS, elsewhere

Not deferred and not missing: curvature-driven refinement is
`SurfaceMeshControls::angularDeflection`, which `P16-SURF-001` qualified.
Netgen's `curvaturesafety` is surface-side and inert here because BetterCAD
supplies the boundary. A second canonical curvature control would be the
competing-state failure `SurfaceMeshControls` already warns about.

## Unit contract

`Length` throughout — dimensioned, SI internally, converted only at the
adapter boundary. There is no `double`-taking overload anywhere in the public
sizing API.

```text
10 mm and 0.01 m      the SAME intent; they compare equal and neither
                      invalidates a mesh built from the other
10 mm and 10 m        different, as they must be
scale check           a 4 mm body at 1 mm and a 40 mm body at 10 mm give
                      median edges differing by roughly ten -- a
                      factor-of-1000 slip would miss by orders of magnitude
```

No display-unit metadata exists in the model, so how a number is shown cannot
change what it means.

## Validation

Run before geometry resolution, and therefore long before the backend.

```text
non-finite size        refused -- CHECKED BEFORE non-positive, because the
                       comparison that decides "positive" answers false for
                       NaN and would report it as non-positive: true, but it
                       hides that the value is not a number
non-positive size      refused (0 and negative)
malformed selector     refused, via core's own FaceSelector rule
two controls, one face refused as DuplicateFaceControl, naming the second
```

## Local sizing foundation

```text
reference      FaceName -- {feature ObjectId, FaceSelector role...}, the
               persistent face identity P12-STREF-001 qualified. NEVER a
               NodeId, ElementId, viewer triangle index or backend entity tag.
resolution     geometry::findNamedFaces, which matches the NAME. No
               nearest-face fallback, no resolution by object name.
region         the surface nodes on that face, then their bounding box
               extended INWARD along the face normal by three target sizes
translation    Ng_RestrictMeshSizePoint per node, Ng_RestrictMeshSizeBox for
               the slab
```

### Selection states

```text
Resolved      the face resolved and a region was produced
Unresolved    the face no longer resolves. The control is KEPT and reported;
              the mesh is REFUSED. Never dropped, never moved.
Unsupported   the face resolved but is a kind that cannot become a region
              (only planar and cylindrical can). Reported, mesh refused.
```

## Conflicts, precedence, order independence

```text
local over global in its region     every backend restriction is a MAXIMUM
same face, two controls             REFUSED before any geometry work
different faces overlapping         the smaller size wins
```

**Order independence is by construction, not by rule.** The minimum of a set
does not depend on arrival order, so there is no insertion order for the
backend to honour. The canonical resolution additionally holds restrictions in
a position-keyed `std::map` and sorts its slabs, so the list handed to the
backend is identical however the controls were written.

## Results

Measured; full tables in [SIZING_RESULTS.md](SIZING_RESULTS.md).

```text
GLOBAL   cylinder r6 h20, deflection 0.05 mm
         target 6.0 mm ->  554 tets, median edge 5.38 mm
         target 3.0 mm -> 1086 tets, median edge 3.09 mm
         target 1.5 mm -> 2185 tets, median edge 2.10 mm
         volume 2.25048e-06 m^3 at every target, geometry valid throughout

BOUND    a 40 mm block's boundary is 12 triangles of 40-57 mm, so targets of
         20 and 10 mm give the IDENTICAL mesh. A tetrahedron cannot be smaller
         than the triangles it conforms to; refining the boundary is
         SurfaceMeshControls' job. Pinned as a test so it is not a surprise.

LOCAL    1.5 mm on the top disc, 6 mm global:
         target region mean edge 3.68 mm, mid-body 9.51 mm
         target region finer AND denser than without the control
         mid-body no finer than without it -- so refinement, not a global
         size change in disguise

WRONG    top region 72 nodes when the top is refined vs 59 when the bottom is
REGION   bottom region 71 nodes when the bottom is refined vs 52 when the top
         is. Both directions.
```

## Geometry conformity, volume, voids

Every sizing case asserts what `P16-VOL-001` established, because a finer mesh
that breaks the geometry is not a sizing pass: `validate()` clean, boundary
conforming in both directions, and the volume recovered — exactly for a
planar-faced body, and from below with convergence for a curved one.

## Regeneration, invalidation, separation

```text
topology-preserving edit (depth 20 -> 60 mm)  control stays Resolved; the
                                              refinement follows the face
unresolvable face                             Unresolved; MESH REFUSED
sizing edit                                   mesh stale
equivalent request (20 mm vs 0.02 m)          mesh NOT stale
display deflection change                     sizing intent untouched
material assign / density edit                mesh NOT stale
stale geometry                                refused by P16-GEOM-001
```

## Determinism

Five runs, identical node count, element count, volume, resolved restriction
list and per-element connectivity, within a preset. Cross-preset mesh
determinism remains unasserted, as `P16-VOL-001` recorded: nothing exports a
mesh to compare.

## Adversarial review

**Four defects found by the review, three of them controls that would have
shipped doing nothing or the opposite of what was asked.** All fixed and
re-verified. Full account in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Regression

Full qualification, `qualification/qualify.cmd`, harness carried unchanged
from `P15-QUAL-001`. Each preset is configured, has **every** build output
removed, is rebuilt with warnings as errors, and only then runs CTest —
unfiltered.

```text
preset             build   warnings  ctest                    build time
debug-ext          exit 0  0         3011/3011 passed (100%)  22 min
release-ext        exit 0  0         3011/3011 passed (100%)  30 min
debug-shared-ext   exit 0  0         3011/3011 passed (100%)  21 min

repeat release-ext   206 tests selected, x5, exit 0
repeat debug-ext     206 tests selected, x5, exit 0

stages failed: 0          qualify.cmd exit 0
started 15:45:08   finished 18:07:03   2026-10-02
```

```text
test count   2983 (P16-VOL-001) -> 3011, exactly +28, all in
             tests/meshing/MeshSizingTests.cpp
executions   3011 x 3 presets + 206 x 5 x 2 repeat presets = 11 093
```

`debug-shared-ext` matters here as it did for `P16-VOL-001`: the sizing types
cross a DLL boundary in that preset.

### Qualified tree == committed tree

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           77ac6c9bb014a033c0a8553031c5551aebcf4742
src               6a1b02d597a4f8b25996c11f65c510c564b1b7d5
tests             0eef09c88af37d315c9ad081a66b4748f54a109b
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Identical before and after the run, and identical to the fingerprint recorded
independently at the freeze. These are the trees that were committed.

## Known limitations

```text
local sizing refines the VOLUME, not the boundary
          The boundary is fixed and validated before the backend is called.
          Surface-local refinement needs P16-MAP-001's facet attribution plus
          a surface-side control, or the Netgen OCC front end -- which ADR-033
          forbids as a bypass of the validated boundary.

the slab is a bounding box, not the face
          For a face small relative to its bounding box, the slab reaches
          material the control did not name. Exact targeting needs facet
          attribution (P16-MAP-001).

coplanar faces facing the same way are not distinguished
          Membership is "on this face's plane, facing its way". Consistent
          with geometry::findFaces, but a limit on what "this face" means.

only planar and cylindrical faces can become regions
          The kinds FaceInfo describes. Others report Unsupported explicitly.

no document object for the controls
          Canonical intent is a value passed to the mesher. A MeshControl
          document object is ADR-030's design and P16-CMD-001/P16-PERSIST-001's
          to build; finding F6 remains open.

cross-preset mesh determinism unasserted
          Carried from P16-VOL-001.

no sanitizer coverage
          This MinGW ships no libasan/libubsan.
```

## Result

```text
RESULT:   PASS
TODO:     21/21 ticked.
NEXT:     P16-QUALITY-001 -- Mesh Quality Metrics / Validation. Not started,
          and not authorized by this document.
```

## Revision

First issue, 2026-10-02.
