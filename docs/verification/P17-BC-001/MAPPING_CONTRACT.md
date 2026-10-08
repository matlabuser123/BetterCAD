# P17-BC-001 — the mapping contract

Who decides what, and what was measured.

## Authority matrix

```text
Entity                    Authority                              Where it lives
-----------------------------------------------------------------------------------------
RestraintId               canonical, persisted document          StructuralRestraint::id_
FaceName                  canonical CAD intent                   StructuralRestraint::face_
RestraintComponents       canonical CAD intent                   StructuralRestraint::components_
BoundaryFacet (ElementId) derived, current mesh                   nowhere -- discarded
NodeId                    derived, current mesh                  PreparedRestraints, disposable
DofIndex                  derived, current solver preparation    ConstraintSet, disposable
ConstraintSet             derived                                PreparedRestraints
FreeEquationMap           derived                                built on demand, never stored
```

`GeometryReference` and `NamedBoundarySet` appear in the brief's matrix as
canonical CAD intent. Neither is used as a restraint target in this milestone:

```text
GeometryReference    there is no such type in the tree. The canonical CAD
                     reference for a face IS `FaceName` (ObjectId +
                     FaceSelector), which is what P16's mapping takes
NamedBoundarySet     exists, on the MeshControl, and names faces by FaceName.
                     A restraint that targeted a named set would reach the
                     same faces through one more level of indirection and
                     would couple structural intent to MESHING intent -- a
                     set edited for meshing reasons would silently change a
                     restraint. Deliberately not done; recorded here rather
                     than shipped as an unused payload
```

## The chain

```text
canonical restraint intent   RestraintId + FaceName + component mask
          |
          |  structural::resolveFaceTarget  (shared with P17-LOAD-001)
          |      boundaryFacetsOf fails          -> selector malformed
          |      !fullyResolved()                -> names no face of this body
          |      facets.empty()                  -> resolved, nothing attributed
          v
current boundary facets      ascending ElementIds of THIS mesh, derived
          |
          |  meshing::boundaryNodesOf  (P16's, which ALREADY sorts and uniques)
          v
unique current NodeIds       ascending, no repeats, deterministic
          |
          |  MeshDofMap::indexOf(NodalDof{node, component})   (P17-DOF's)
          v
DofIndex per requested component
          |
          |  buildConstraintSet  (P17-DOF's, which sorts)
          v
ConstraintSet                ascending, unique, stamped with the mesh
```

**P17-BC numbers nothing.** There is no `3 * nodeId` anywhere in
`src/structural/StructuralConstraints.cpp` — that arithmetic is the defect
P17-DOF-001 exists to prevent, and a second copy of it here would reintroduce
it. Every index comes from `MeshDofMap::indexOf`, and the test decodes each one
back through `MeshDofMap::dofAt` to prove it names a node of the target and a
requested component.

**And it deduplicates nothing**, which was the audit's main finding.
`meshing::boundaryNodesOf` ends with

```cpp
std::ranges::sort(nodes);
nodes.erase(std::ranges::unique(nodes).begin(), nodes.end());
```

and also re-checks the map against the mesh. So brief sections 18
(deduplicate), 19 (deterministic order), 112 (shared facet nodes) and 114
(efficient deduplication) are satisfied by **one call**, and writing any of it
again here would have been a second definition of the node set.

## What is shared with P17-LOAD-001, and what is not

The three resolution checks moved into `structural::resolveFaceTarget`
(`include/bettercad/structural/StructuralTarget.hpp`) and both consumers call
it. They are P16's contract, so one copy is correct and two would drift.

The **diagnostics** are not shared: a load "has nothing to act on" and a
restraint "has nothing to constrain", and each message carries its own
identity. `TargetProblem` is deliberately neutral and each consumer maps it
onto its own problem enum.

**This made `src/structural/StructuralLoad.cpp` a changed production file**, so
P17-LOAD-001's qualification is re-established by this milestone's run. See
[REQUALIFICATION.md](REQUALIFICATION.md).

## Mapping table

Measured, not asserted in prose. Each row is a `WARN` emitted by the test that
produced it, and every row's expected count is `unique nodes x components`
computed from a node set the TEST assembled facet by facet from P16's mapping —
never from `boundaryNodesOf`.

```text
Model              Target            State      Facets  Nodes  Components  Expected  Actual  PASS
-------------------------------------------------------------------------------------------------
RM-MESH-01         end cap           resolved        2      4  ux                 4       4  PASS
RM-MESH-01         end cap           resolved        2      4  uy                 4       4  PASS
RM-MESH-01         end cap           resolved        2      4  uz                 4       4  PASS
RM-MESH-01         end cap           resolved        2      4  ux+uy              8       8  PASS
RM-MESH-01         end cap           resolved        2      4  fixed             12      12  PASS
RM-MESH-01         both caps         resolved      2+2    4+4  ux                 8       8  PASS
RM-MESH-02         lateral wall      resolved       72     72  uz                72      72  PASS
RM-MESH-02         lateral wall      resolved      100    100  uz               100     100  PASS
RM-MESH-02         lateral wall      resolved      200    200  uz               200     200  PASS
RM-MESH-03         hole wall (named) resolved       >0    >0   fixed           3 N     3 N   PASS
RM-MESH-03         unnamed wall      UNRESOLVED      -      -  fixed         refused refused PASS
RM-MESH-04         inner wall        resolved       72     72  ux                72      72  PASS
RM-MESH-04         bottom annulus    resolved       >0     72  fixed            216     216  PASS
RM-MESH-06 base    datum face        resolved        2      4  ux                 4       4  PASS
RM-MESH-06 placed  datum face        resolved        2      4  ux                 4       4  PASS
RM-MESH-07 12 mm   refined face      resolved        2      4  fixed             12      12  PASS
RM-MESH-07  6 mm   refined face      resolved        2      4  fixed             12      12  PASS
RM-MESH-07  3 mm   refined face      resolved        2      4  fixed             12      12  PASS
block fixture      end cap           resolved        2      4  fixed             12      12  PASS
```

The three RM-MESH-02 rows are the same canonical target at 0.40, 0.10 and
0.025 mm surface deflection. The three RM-MESH-07 rows are the same canonical
target at 12, 6 and 3 mm local sizing.

## Two measured facts that constrain what can be claimed

**A planar face of a box carries exactly its CAD corners, at any sizing.**
RM-MESH-07's refined face stays at 2 facets and 4 nodes while the body goes
from 14 to 61 to 64 nodes: a plane is exactly representable, so nothing in the
sizing controls retriangulates it. This is recorded rather than asserted away,
because it means two claims the brief asks for cannot be made on a box:

```text
brief section 131   "do not constrain only CAD corner vertices" -- on a box
                    face the correct answer IS the four corners, so a
                    corners-only implementation is indistinguishable there.
                    Made on RM-MESH-04's inner wall instead: 72 mapped nodes
                    against a face bounded by two circular edges

brief section 99    "mapped node count may change with refinement" -- it does
                    not, on a planar face. Made on RM-MESH-02's cylindrical
                    wall instead, where the same canonical target gives 72,
                    100 and 200 nodes and the constrained count follows it
                    exactly at every level
```

**The unit fixture needed a local control to have off-target nodes at all.**
On the default sizing the 40 x 30 x 20 mm block meshes to 9 nodes, every one of
them on some face. 20 mm globally with 6 mm on one side face — RM-MESH-07's own
ratio — takes it to 19, so most of the mesh is off any single target and the
off-target half of the semantic check has something to find.

## Nothing here classifies geometry

Searched over `StructuralConstraints.cpp`, `StructuralRestraint.hpp`,
`StructuralConstraints.hpp` and `StructuralTarget.cpp`:

```text
nearest      0 occurrences anywhere
distance     1, in a COMMENT in StructuralTarget.hpp saying nothing here
             measures one
centroid     1, in the same comment
tolerance    0
normal       2, both in comments: StructuralTarget.hpp on not comparing one,
             StructuralRestraint.hpp on the convention NOT being face-local
3 * nodeId   1, in a comment in StructuralConstraints.cpp saying there is none
```

Every match is prose. There is no executable occurrence of any of them.

Facet geometry is never read at all in this milestone: a restraint needs the
facets' NODES, not their area, their normal or their position. P16 decides which
triangles belong to which CAD face, and this module asks.
