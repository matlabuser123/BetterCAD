# P17-BC-001 — the restraint schema

What a restraint IS, what it is NOT, and how each claim is enforced rather than
reviewed.

## The record

```text
class StructuralRestraint
    RestraintId          id_            persisted document identity (P17-DATA-001)
    FaceName             face_          canonical CAD target, and the only kind
    RestraintComponents  components_    which displacement components are held at zero
```

`sizeof(StructuralRestraint) == 120` bytes on this toolchain, which is the
`sizeof` of a mirror struct declaring those three members in that order.
A fourth member changes it and the assertion fails:

```text
tests/structural/StructuralBCTests.cpp   StructuralBC_CanonicalRestraintsCarryNoMeshHandle
```

## Persisted against derived

```text
PERSISTED (canonical)              DERIVED (rebuilt every preparation)
---------------------------------  --------------------------------------------
RestraintId                        meshing::ElementId  boundary facets
FaceName (ObjectId + FaceSelector) meshing::NodeId     current nodes
RestraintComponents                DofIndex            solver indices
                                   ConstraintSet       the constrained set
                                   PreparedRestraints  the whole preparation
```

`RestraintId`'s own declaration in `core/Id.hpp` wrote the rule before this
milestone existed:

> A restraint's identity is the intent; the constrained DOF indices are a
> derived consequence of the current mesh and the current numbering, and are
> never its identity.

The roles are never reversed, and six build-failure cases enforce it rather
than a reviewer:

```text
compile_fail.structbc.restraint-from-node-id
compile_fail.structbc.restraint-from-dof-index
compile_fail.structbc.components-from-integer
compile_fail.structbc.components-as-integer
compile_fail.structbc.components-from-displacement
compile_fail.structbc.prepared-restraints-constructed-directly
```

## Nonzero prescribed displacement supported?

```text
NO
```

And there is **no field for one**. The schema carries a component mask and
nothing else, because a `double value = 0` that only ever supported zero would
be a false capability: a caller could set it, nothing would read it, and the
model would be silently wrong. `RestraintComponents` is one byte, is not
constructible from a `double` or a `Length`, and does not convert to an
integer — all three asserted.

A later milestone adds prescribed displacement by adding a value alongside the
mask. `RestraintId`, the CAD target and the resolution chain are untouched by
that change, which is what makes the deferral a deferral and not a gap.

## The component mask

```text
class RestraintComponents          one std::uint8_t
    along(DofComponent)            one component
    fixed()                        all three -- the ONE spelling of "fixed"
    add(DofComponent)              idempotent
    holds(DofComponent)            asks in component terms
    count() / isEmpty() / isFixed()
    unionWith(RestraintComponents) what two restraints on one node hold together
```

Built from P17-DOF's `kDofComponents` and `offsetOf`, so the bit order cannot
drift from the degree-of-freedom ordering it indexes. A strong type rather than
a `1 | 2 | 4` bitfield for the same reason `DofIndex` is a strong type: a bare
integer mask is interchangeable with a count, an offset and an index, and three
of those four are wrong in any given position.

Asserted at compile time beside the definition:

```cpp
static_assert(RestraintComponents::along(DofComponent::Ux)
                  .unionWith(RestraintComponents::along(DofComponent::Uy))
                  .unionWith(RestraintComponents::along(DofComponent::Uz)) ==
              RestraintComponents::fixed());
```

**An empty mask is representable and invalid.** Representable because that is
what a default-constructed value is; invalid because a restraint that holds
nothing at zero is not a weaker restraint, it is a record whose intent cannot
be acted on. `validate(StructuralRestraint)` refuses it, and
`prepareStructuralRestraints` refuses it again before touching the mesh.

## Components are GLOBAL axes

`Ux` is global X, whatever the face's normal is doing. The convention is frozen
here, and measured on RM-MESH-06 — the same block and the same block rigidly
transformed, where a `ux` restraint on the datum face constrains the same four
nodes on both models although the face's normal has rotated:

```text
tests/reference/StructuralBCReferenceTests.cpp
    StructuralBC_KeepsGlobalComponentsUnderARigidTransform
```

Normal-only, tangential, frictionless and roller supports would each need a
face-local basis. None exists in the schema, so none can be reached by
accident, and adding one is a deliberate later change rather than a
reinterpretation of this one.

## One target kind, and not a variant

`StructuralLoad` is a `std::variant` because its four payloads carry different
units and different target kinds. A restraint has one payload shape, so a
variant of one would be noise. An edge or vertex restraint would need a
canonical edge or vertex reference P16 does not provide; adding one later is a
change to this type, not a reason to pre-build a variant for it now.

## Where the restraints live

```text
StructuralAnalysisDefinition
    MeshControlId                      mesh
    StructuralAnalysisMode             mode
    std::vector<StructuralLoad>        loads        (P17-LOAD-001)
    std::vector<StructuralRestraint>   restraints   (P17-BC-001)
```

In the definition, which is what gives restraint editing its invalidation
semantics **with no new mechanism** — see
[REMESH_VALIDATION.md](REMESH_VALIDATION.md). The mirror assertion in
`tests/structural/StructuralDataTests.cpp` was extended by one member, which is
the point at which a field's authority is stated: anything derived still cannot
be put there.

## What the document does NOT check

`validate(StructuralAnalysisDefinition)` checks the mesh handle and does not
walk the restraints — the same choice P17-LOAD-001 made for loads. A record is
checked against a mesh when it is prepared, and `validate(StructuralRestraint)`
is available to a caller that wants the mesh-free record check on its own.
Adding a document-level walk for restraints but not for loads would make the
two inconsistent for no gain, so it was not done; it is recorded here as a
decision rather than left as a silence.
