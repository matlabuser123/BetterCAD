# P17-LOAD-001 — the load schema and its conventions

```text
SUBJECT:  what a load IS, what it carries, what it may never carry, and the
          conventions frozen for every later P17 milestone
```

## The four load types

| Type | Canonical target | Physical input | Unit | Direction |
| --- | --- | --- | --- | --- |
| `NodalForceLoad` | `NodeId` + `MeshStamp` — **mesh-local** | `Force3D` | N | global |
| `SurfaceTractionLoad` | `FaceName` | `Traction3D` | Pa | **global, fixed** |
| `PressureLoad` | `FaceName` | `Pressure` (scalar, signed) | Pa | **the current outward normal** |
| `GravityLoad` | the whole body | `Vector3D` acceleration | m/s² | global |

Derived, in every case: `NodeId -> Force3D`, in N, rebuilt on every
preparation and never persisted.

**A variant, not a tag and a bag of doubles.** The four payloads have different
units and different targets, so `struct { int type; double a, b, c, d; }` would
make a pressure assignable from a traction and lose every unit. Each is its own
type and the compiler keeps them apart:

```text
is_assignable_v<SurfaceTractionLoad&, PressureLoad>     false
is_assignable_v<PressureLoad&, SurfaceTractionLoad>     false
is_assignable_v<Force&, Pressure>                       false
Pressure * Area                                         IS a Force, by dimension
Force * Length                                          IS a Torque
Torque                                                  IS Energy -- N m shares
                                                        a dimension with J, and
                                                        the alias says so
```

## What a canonical load may never carry

Asserted at compile time rather than reviewed, with mirror structs that declare
the same members in the same order so an added field changes the size:

```cpp
struct PermittedTraction { FaceName face; Traction3D traction; };
struct PermittedPressure { FaceName face; Pressure magnitude; };
static_assert(sizeof(SurfaceTractionLoad) == sizeof(PermittedTraction));
static_assert(sizeof(PressureLoad)        == sizeof(PermittedPressure));
static_assert(!is_constructible_v<SurfaceTractionLoad, meshing::ElementId>);
static_assert(!is_constructible_v<PressureLoad,        meshing::ElementId>);
static_assert(!is_constructible_v<SurfaceTractionLoad, meshing::NodeId>);
static_assert(sizeof(GravityLoad) == sizeof(Vector3D));
static_assert(!is_constructible_v<GravityLoad, Density>);
```

The last two are ADR-028 in compile-time form: a gravity load carries an
acceleration and **no density**, because a solver maintains no material data of
its own.

The same instrument guards the analysis definition, which now holds the loads:

```cpp
struct PermittedDefinition {
    MeshControlId mesh;
    StructuralAnalysisMode mode;
    std::vector<StructuralLoad> loads;
};
static_assert(sizeof(StructuralAnalysisDefinition) == sizeof(PermittedDefinition));
static_assert(!is_constructible_v<StructuralAnalysisDefinition, StructuralResult>);
static_assert(!is_constructible_v<StructuralAnalysisDefinition, meshing::MeshStamp>);
```

**That instrument replaced one P17-DATA-001 predicted would break.** Its note
said: *"Trivial copyability holds only until P17-LOAD-001 gives the definition a
collection of loads, which is a legitimate intent field and will break it. At
that point the property to assert is still 'no derived state' and the
instrument has to change again — a compile-fail case naming the types that may
not appear would survive it."* It did break, and the replacement is what that
note named.

## Units

```text
coordinates   m        force          N
traction      Pa       pressure       Pa
acceleration  m/s^2    density        kg/m^3   (P15's, never the load's)
area          m^2      moment         N m
```

`Traction3D` and `Moment3D` were added to `core/math/Vector.hpp` beside
`Force3D`, and `Torque` to `core/units/Units.hpp` beside `Stress`. That is the
placement `Force3D`'s own comment establishes — *"in `core` rather than in
`structural` because a force is an engineering quantity and not a
structural-analysis concept — P18's heat flux will want the same treatment"* —
and a traction and a moment are engineering quantities on the same footing.

The cost is stated: it widens this milestone's blast radius to everything that
includes `core`, which is why the regression's repeat set is the broad one.

## Direction conventions — FROZEN

### Traction is global and does not follow the surface

`Traction3D` is resolved on the model's own X, Y and Z. Rotating the body does
**not** rotate the load. A traction of `[0, 0, -1] Pa` keeps pulling along −Z
whatever the face does.

### Pressure follows the current outward normal, and positive acts INWARD

```text
    t = -p n_out
```

With P16's outward boundary orientation, a positive `magnitude` compresses the
body. Demonstrated on a face whose normal is known:

```text
the block's end cap          outward normal +Z
positive pressure p          resultant force  [0, 0, -p A]      INWARD
the block's START cap        outward normal -Z
the SAME positive pressure   resultant force  [0, 0, +p A]      also inward
```

Two faces of one body, opposite signs, from one scalar — which is what
"follows the normal" means and what a global-direction pressure could not do.

**Signed, deliberately.** A negative pressure is outward suction, which is
ordinary in linear statics, so it is accepted rather than clamped or refused. A
clamp would silently change the user's model. Tested.

**Stored as a scalar, never as a frozen vector.** The direction comes from the
mesh that is current, so a load created before a remesh, a geometry edit or a
rigid transform still means the right thing afterwards. Freezing a force or a
traction vector at creation time is the defect this representation exists to
prevent — and the transform test is what proves the difference:

```text
RM-MESH-06, the same block under a compound rotation mixing all three axes
  PRESSURE   resultant rotates:  F_placed = R F_base, magnitude invariant
  TRACTION   resultant does NOT: F_placed = F_base, component for component
```

Requiring either behaviour of the other would be wrong, and the test says which
is which.

## The nodal-force policy

**Policy A, mesh-local, as the brief prefers.** `NodalForceLoad` carries the
`MeshStamp` it was created against and is refused against any other mesh —
including one with the same node count and the same numeric handles, which a
remesh of an unchanged model produces.

```text
same mesh                     applied exactly, no integration
node not in the mesh          refused, NodeNotInMesh, no nearest-node fallback
after a remesh                refused, "belongs to a different mesh generation"
                              -- and the handle is still a valid node of the new
                              mesh, which is why the STAMP decides
rebound to the new stamp      accepted, which is the intended recovery
```

What it is for: element and solver fixtures, where a test wants a force at a
known node. What it is **not**: durable point-load intent on the model. A
durable point load would be a load on a CAD **vertex**, which needs a canonical
vertex reference P16 does not provide — recorded as a limitation rather than
faked with a node handle.

## Integration

### Surface, per facet

```text
    F_facet = A t          each corner receives A t / 3
```

The consistent load vector of a three-node linear triangle under a constant
load: each shape function integrates to `A/3` over its own triangle, so the
three thirds sum to `A t` exactly. Not `A/2`, and not the whole force on one
corner — both are mutation probes and both are killed.

The area vector carries direction and magnitude together:

```text
    A_vec = (1/2) (p2 - p1) x (p3 - p1)
```

The **half** is not optional: the cross product of two edges spans the
parallelogram. For a pressure the unit normal is recovered by dividing by the
magnitude and the area is then applied once, by `facetNodalForce` — applying
the area vector *and* the area would square it, which has its own assertion.

### Accumulation ADDS, never overwrites

A node shared by several loaded facets receives a contribution from each, and a
node reached by two loads receives both. Superposition is the semantics of
linear statics, so overlapping targets are not an error — refusing them would
decide the user's model for them. Overwriting is a mutation probe and is killed
by ten tests.

### Body force, per tetrahedron

```text
    b = rho g              F_tet = rho V g       each node receives rho V g / 4
```

Each Tet4 shape function integrates to `V/4` over its own element. `V` is
P16's **signed** volume with no absolute value: a `StructuralModel` proves
every element is positively oriented, so taking a magnitude could only hide a
violation of that.

```text
    rho [kg/m^3] x V [m^3] x g [m/s^2]  =  kg m/s^2  =  N
```

## Gravity: IMPLEMENTED

Not deferred, and the decision is recorded in `GRAVITY_DECISION.md` with its
reasons. The short form: P17-MAT-001 already resolves a density for
`ConsumerKind::FeaLinearStaticWithGravity` by reading P15's requirement table,
so the density path is qualified; `StructuralMaterial::density()` is present
exactly when the analysis mode asks for self-weight; and the integration is
four lines over a mesh whose element volumes are already available.

```text
density source            P15, through StructuralMaterial::density()
no density, gravity asked  refused, DensityMissing -- there is no 7850 anywhere
no gravity in the set      the density is never consulted
direction                  explicit; kStandardGravity is offered, not a default
```

## Determinism

```text
loads               processed in the order given
facets              P16's ascending, deduplicated set
nodal accumulation  std::map keyed on NodeId -- ascending, and the same
                    floating-point summation order in every preset
emitted field       ascending by NodeId; a node with no load is ABSENT rather
                    than present with a zero force, so the field's size says
                    how much of the mesh is loaded
```

No unordered container appears in the module. Replacing the `std::map` with an
`unordered_map` is a mutation probe and is killed by the determinism and
order-independence tests.
