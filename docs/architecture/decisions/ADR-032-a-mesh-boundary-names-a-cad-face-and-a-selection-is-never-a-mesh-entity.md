# ADR-032 — A mesh boundary names a CAD face, and a selection is never a mesh entity

```text
STATUS:    Accepted
DATE:      2026-09-30
MILESTONE: P16-ARCH-001
TOUCHES:   core/document/References.hpp (FaceName, FaceSelector), ADR-024 (a
           chamfer edge selection has an identity), ADR-028 (solvers hold no
           material data), core/geometry/Mesh.hpp's winding convention,
           core/Id.hpp's unused FaceId
```

## Context

This is the contract P17 rests on. A structural analysis is not "a mesh" — it is a mesh plus
statements of the form *this face is fixed*, *that face carries 2 MPa*, *this region is steel*.
Each statement has to survive a remesh, because a user changes an element size and expects
their boundary conditions to still be there.

The audit established two things that decide the design.

**BetterCAD already has a stable, generative face reference.** `FaceName` names a face by the
feature that generated it, its role there, the profile entity that swept it, and the chain of
pattern and mirror copies made of it — never by index. `features::checkFaceName()` resolves it
and fails in a structured way: `NotFound` when the feature is gone, when the profile entity is
gone, or when the feature's own operation removed the face; `InvalidArgument` when the selector
is malformed.

**There is a trap next to it.** `core/Id.hpp` declares `FaceId`, `EdgeId` and `VertexId`, and
`grep -rn "\bFaceId\b" include/ src/` returns exactly one hit: the alias's own definition. They
are used nowhere, and their comment says why — *"Topology IDs; unique within their body.
Persistent naming across regenerations is future work (semantic topology naming)"*. They are a
P21 placeholder. They look like precisely the right type for "which CAD face does this facet
belong to", and adopting them would give the mapping an identity with no defined behaviour
across the regeneration the mapping exists to survive.

## Constraints

- P16's invariants: "Geometry references and mesh identities must remain separate", "Generated
  nodes/elements are never treated as CAD geometry", "Element orientation must be defined and
  validated".
- ADR-031: no mesh handle may be persisted or stored in canonical intent.
- ADR-028: solvers hold no material data.
- P15 gives one optional material per document. `ComponentDefinition` has no material field.
- A `Body` is "zero or more solids" (`Body.hpp`), and `TopologySummary` counts them, so a
  multi-solid body is an ordinary case and not an error.
- Preserving semantic face identity *across a topology change* is P21. P16 must not claim it.

## Options

**1. A selection is a set of mesh facets.** What many meshers expose, and it fails the one
requirement that matters: every remesh destroys it. Storing facet indices in a document would
also persist derived state, which ADR-030 forbids.

**2. A selection names CAD geometry; the mesher maps facets to it.** A boundary condition is a
CAD-level statement, resolved to facets when a solve needs them.

**3. Option 2, but keyed by `FaceId`.** Uses a type that already exists and has no cross-
regeneration semantics, so the reference would be stable only by accident.

## Decision

**Option 2, keyed by `FaceName`.**

**A selection is never a mesh entity.** Local sizing, boundary conditions, loads and restraints
name CAD geometry — a `FaceName`, and later the edge and vertex equivalents. They are stored in
canonical intent, they are persisted, they are undoable, and **no remesh can touch them**,
because they do not refer to the mesh at all.

This is the whole answer to "can selections survive remesh?" and it is a definitional answer
rather than a mechanism: they survive because they were never mesh selections. There is no
remapping step to get wrong, no tolerance to tune, and no state to migrate.

**The mapping points from the mesh to the CAD, never the reverse.** Every boundary facet records
**which CAD face** it was generated from, and the mesh carries that face's `FaceName` **set**. To
apply a load to a face, the mesher resolves the user's `FaceName` and gathers the facets whose
face carries it; if it resolves to no facets, that is a diagnostic, never an empty set silently
treated as "no load".

**The face-to-name relation is many-to-many, and the mesh must store it as one.** This is not a
design choice; it is what `Faces.hpp` already documents about how names propagate:

> "a face split in two carries its name on both parts, faces merged into one carry all their
> names, a face the operation removed carries none."

So one name may cover several faces, and one face may carry several names. A facet field of type
`optional<FaceName>` would be wrong on both counts, and an attribution keyed on a name being
unique to a face would be wrong after the first boolean.

**An unnamed face is normal and is not a failure.** `Faces.hpp` also says "Other operations give
bodies without names", and the audit confirmed the consequence at its sharpest: `makeBox`,
`makeCylinder` and `makeSphere` take no namer, so **a primitive's faces carry no `FaceName`**.
An attribution rule that treated an empty name set as a meshing failure would refuse to mesh a
box — which is why this ADR does not have one. An empty name set means the face was made by an
operation that reports no history, and the only consequence is that the face cannot be *targeted*
by a `FaceName` — already true of such a face everywhere else in BetterCAD. The facet is still
attributed to its face, so it is still addressable through the mesh's own face table, and
geometric selection (`FaceSignature`) still works on it.

The failure this rule does define is the other direction: a `FaceName` the user supplied that
resolves to no facets.

**`FaceId` is not used**, and `core/Id.hpp` is left untouched. When P21 delivers semantic
topology naming, the mapping gains a stable identity underneath `FaceName` without changing this
contract. Until then a topology-changing edit leaves a reference explicitly unresolved — a
reported state, exactly as a dangling material assignment is in P15 — and never silently bound
to a different face.

**Orientation is inherited from the existing convention, not invented.** `core/geometry/Mesh.hpp`
already documents its triangles as "counter-clockwise seen from outside the solid". Boundary
facet normals therefore point **out of the material**. A tetrahedron's nodes are ordered so that
its Jacobian determinant is positive. Both are validated, and an inverted or degenerate element
is a failure, never a warning.

**Holes and internal voids are not special.** A hole's wall is boundary with a `FaceName` like
any other face; an internal void is an inner shell whose facets are boundary facets whose normals
point out of the material, which is to say into the void. The mesh has no concept of a hole and
needs none — "out of the material" is already unambiguous for a void.

**A multi-solid body meshes as one mesh with one region per solid, and nodes are never shared
between regions.** An empty body — zero solids — is refused. Two solids that touch produce
coincident but distinct nodes on the shared interface. That is correct for a contact formulation
and wrong for a bonded one, and **P16 does not bond and does not detect contact**: assembly
contact meshing is out of P16's scope by its own scope boundary. The consequence is stated here
rather than left for P17 to discover: a multi-solid part meshed by P16 is mechanically a set of
independent solids.

**A mesh is in the coordinates of the body it came from, and carries no transform.** Patterns and
mirrors bake their transforms into the body during regeneration, so a patterned instance is
already in model coordinates and needs nothing special. P16 does not mesh assembly occurrences.
When it does, it will mesh the definition and let the consumer apply the occurrence placement,
which is sound only because a `Placement` is rotation and translation with no scale field — a
rigid transform, which moves a mesh without changing any quality metric. Quality metrics are
computed in the body's frame and are frame-independent for that reason.

**A mesh holds no material data.** P15 gives one material per document, so today every region of
every mesh has the same material, and P16 adds no material field anywhere: not to a mesh, not to
a region, not to an element. Material is resolved from the document's assignment at the point of
consumption, through P15's `requireLinearElasticConstants` and `requireDensity`, which exist so
that a solver receives a complete set or an explicit diagnostic. The region is the unit a future
per-region material would attach to; P16 does not anticipate it with a field.

## Consequences

- Boundary conditions cost nothing to preserve across a remesh, and there is no remapping code to
  be wrong.
- The mesher must be able to attribute every boundary facet to a CAD face. That constrains the
  backend interface — a backend that returns a bare tetrahedralization with no face tagging
  cannot be used, which ADR-033 makes an admission requirement.
- A topology-changing edit can leave a load unresolved. It is reported, and the user re-selects.
  That is the honest behaviour until P21, and it is better than a load silently moving to another
  face.
- P17 must express everything in CAD terms. It cannot hold a facet list.
- A multi-solid part is not bonded. P17 must not assume otherwise, and a reference model should
  pin it.

## Validation

```text
P16-MAP-001      every boundary facet is attributed to exactly one CAD face; the
                 facet count per face is reported; a primitive with NO named
                 faces meshes and maps successfully
P16-MAP-001      a name covering two faces gathers the facets of both; a face
                 carrying two names is gathered by either
P16-MAP-001      a load on a face survives a size change and a remesh with the
                 same resolved face and a DIFFERENT facet set
P16-MAP-001      a topology-changing edit leaves the reference unresolved and
                 reported, and bound to nothing else
P16-QUALITY-001  positive Jacobian for every element; outward normals checked
                 against the closed-volume test the STL tests already use
P16-VOL-001      a two-solid body gives two regions sharing no node; an empty
                 body is refused
P16-QUAL-001     grep: no material property value anywhere in the mesh data model
```

## Invariants

```text
A selection, a load, a restraint and a local sizing name CAD geometry. None of
them ever names a node, an element or a facet.

Every boundary facet is attributed to the CAD face it came from, and the relation
between a face and its FaceNames is stored as the many-to-many relation it is. A
face with no name is normal, not a failure.

A user-supplied FaceName that resolves to no facet is a diagnostic, never an
empty set treated as "nothing to do".

An unresolvable reference is reported as unresolved and is never rebound to a
different face.

Boundary facet normals point out of the material; element Jacobians are positive.
Both are validated, and a violation is a failure.

A mesh has one region per solid, shares no node between regions, and refuses an
empty body.

A mesh is in its body's coordinate frame and carries no transform.

No mesh, region or element holds a material property value.
```
