# P17-DATA-001 — the result data model

```text
SUBJECT:  how a structural solution is represented, in what units, keyed how,
          and what the container refuses
```

## The unit matrix

| Field | Representation | Internal unit | Dimension | Canonical? |
| --- | --- | --- | --- | --- |
| Node coordinates | `Point3D` of `Length` | m | length | derived, P16's |
| Nodal displacement | `Translation3D` | **m** | length | derived |
| Nodal reaction | `Force3D` | **N** | force | derived |
| Element strain | `Strain6` | **dimensionless** | — | derived |
| Element stress | `Stress6` of `Stress` | **Pa** | pressure | derived |
| Young's modulus | `ElasticModulus` | Pa | pressure | canonical, P15's |
| Poisson ratio | `PoissonRatio` | dimensionless | — | canonical, P15's |

SI internally, display conversion at adapters. No field is a bare `double`
except strain, which is a ratio and has no quantity to be.

```cpp
static_assert(std::is_same_v<decltype(Translation3D::x), Length>);
static_assert(std::is_same_v<decltype(Force3D::x), Force>);
static_assert(std::is_same_v<decltype(Stress6::xx), Stress>);
static_assert(std::is_same_v<decltype(Strain6::xx), double>);
```

### Two types reused, one added

**`Translation3D` already existed** — "a translation in model space: how far
every point moves along X, Y and Z", three `Length` components. That is a nodal
displacement exactly. Defining a `Displacement3` beside it would have been the
duplicate unit machinery this milestone must not create.

**`Force3D` is new**, placed in `core/math/Vector.hpp` beside its sibling
rather than in `structural/`: a force is an engineering quantity, not a
structural-analysis concept, and P18's heat flux will want the same treatment.
It carries a `sum(std::span<const Force3D>)` whose documentation states the
thing that matters for a future equilibrium check — **floating-point addition
is not associative**, so the caller's order is part of the answer, and every
caller in BetterCAD iterates a mesh whose enumeration is ascending and
deterministic.

## Component order, frozen

```text
XX  YY  ZZ  XY  YZ  ZX
 0   1   2   3   4   5
```

Frozen here once for every P17 milestone: `P17-ELEM-001` builds a 6×12 `B` and
a 6×6 `D` indexed this way, `P17-POST-001` reads them back, a GUI or CLI prints
them. A module that chose its own order would produce stress that is wrong in
the shear terms only — the hardest kind of defect to see.

**Not the same order as `InertiaTensor`**, which lists `xx, yy, zz, xy, xz,
yz`. Said out loud in the header because the two are six-component symmetric
tensors in one codebase and a loop copied between them would be wrong. They
differ for a reason: this is the cyclic Voigt convention the isotropic
elasticity matrix is conventionally written in, while an inertia tensor is
never written as a Voigt vector and its field order carries no numerical
meaning.

`StructuralData_TheTensorComponentOrderIsFrozen` asserts each enumerator's
value and checks that `component()` agrees with the named field for all six, so
a reordered switch fails rather than silently returning a neighbour.

## The shear convention, in the field names

```text
gammaXy = 2 * epsilonXy
```

**Engineering shear**, and the convention is carried by the NAME rather than by
a comment:

```cpp
struct Strain6 {
    double xx, yy, zz;
    double gammaXy, gammaYz, gammaZx;
};
```

A reader who sees `gammaXy` beside `xx` cannot take it for the tensor
component. Six fields named `xx..zx` with a comment saying "engineering" is
exactly how a factor of two gets lost between two milestones.

The constitutive matrix `P17-ELEM-001` writes must match: with engineering
shear the isotropic `D` carries `mu` on its shear diagonal, not `2 mu`.

**The test names the fields**, so the rename that would reintroduce the
ambiguity stops the test suite compiling. A search of the header's text would
merely stop matching, which a reader could talk themselves past.

Stress needs no such care: a `tau_xy` is a `tau_xy` either way, and the factor
of two lives on the strain side alone.

## How the arrays are keyed

```text
displacements   DENSE, parallel to mesh.nodes().     one per node
strains         DENSE, parallel to mesh.tetrahedra(). one per Tet4
stresses        DENSE, likewise.
reactions       SPARSE, each carrying its NodeId, ascending.
```

**Dense where every entity has a value, sparse where only some do.** A reaction
exists only where the model is restrained — a handful of nodes out of thousands
— and a dense array would be mostly zeros that cannot be told from a genuine
zero reaction at a restrained node.

### Not indexed by raw `NodeId`, and that is the point

P16 allocates node IDs **from 1**, and `MeshBuilder::addNode(NodeId, Point3D)`
lets a caller choose them. So nothing guarantees `0..N-1`. Indexing a vector by
`id.value()` would be correct for every mesh the current backend happens to
produce and wrong for the first one that is not — a latent defect that passes
every test until it does not.

The qualified access model is the mesh's own enumeration: `Mesh` documents that
nodes are stored and enumerated in **ascending NodeId**, with no unordered
container anywhere, so the same construction gives the same order in every
preset. `displacementOf(mesh, node)` binary-searches that ascending span, which
is O(log n) and needs no second index.

```text
grep for '[x.value()]' indexing in structural/     0 occurrences
grep for unordered_map / unordered_set             0 occurrences
```

## What `create` refuses

| Refusal | Why |
| --- | --- |
| the source does not name this mesh | checked **first**, because every other check is against `mesh` and would otherwise validate against a mesh the result does not claim |
| a mesh with no nodes or no elements | an empty mesh is not a solved model. P16 already refuses to publish a zero-element mesh, so this is defence at the next boundary |
| `displacements.size() != nodeCount` | |
| `strains.size() != tetrahedronCount`, likewise stresses | |
| any non-finite value | NaN or infinity in a published result is a failed solve, not a worse one |
| reactions out of ascending order | the order is part of the contract: a caller summing them for equilibrium must get the same floating-point answer every run |
| a reaction on a node not in this mesh | |

Each is a section of `StructuralData_AResultMustMatchItsMeshExactly`.

**A partially populated result is refused as a whole.** A failed solve must not
call `create` with half its channels filled: that is not a worse result, it is
a result claiming to be a solution. `P17-SOLVE-001` publishes only on success
and keeps the previous one as stale otherwise — the behaviour
`Mesher::generate` already has for a failed remesh.

## Binding to the mesh

```cpp
bool describes(const VolumeMesh&) const;   // compares MeshStamp, never counts
```

Measured: remeshing the same body with the same settings gives **9 nodes either
way** and a different stamp, and `describes` returns false. `displacementOf`
then refuses with "this result was not computed on that mesh" rather than
mapping every value onto the wrong material.

## Immutability

```text
no setter                       source() returns const&, and there is no other
                                accessor
no mutable view                 every array is handed out as a span of CONST
                                elements -- a renderer holding a result cannot
                                write a stress back into it
provenance cannot be rewritten  which is what stops a stale result being
                                "refreshed" without recomputing it
```

Asserted at compile time on the element types the spans yield.

A result **is** assignable, deliberately: the solver service that holds one per
analysis must be able to replace it when a new solve succeeds. That is
replacement by the owner, not mutation through a reference, and the distinction
is worth keeping — the first draft of the test asserted non-assignability, which
was a property neither required nor desirable, and the compiler said so.

## Storage

```text
O(nodes + elements), and the mesh is NOT copied in.
```

A result records a twelve-byte `MeshStamp` and its own arrays. Duplicating a
hundred thousand nodes per solve is the obvious way to make results unusable at
scale, and is asserted against:

```cpp
static_assert(sizeof(StructuralResult) < sizeof(meshing::VolumeMesh));
static_assert(sizeof(StructuralResult) <= 4 * sizeof(std::vector<int>)
                                          + sizeof(StructuralResultSource) + 64);
```

A `sizeof` check rather than a search for a member name, because a member can be
renamed and a size cannot be argued with.

## Expected cardinality, for the milestones that fill these

```text
displacements  == mesh.nodeCount()
strains        == mesh.tetrahedronCount()        constant within a linear Tet4
stresses       == mesh.tetrahedronCount()
reactions      <= mesh.nodeCount(), and equals the number of restrained nodes
```

Enforced by `create`, so a solver that produced the wrong count is refused
rather than publishing a result that is quietly short.
