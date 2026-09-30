# ADR-031 — A mesh node is a handle, not an identity

```text
STATUS:    Accepted
DATE:      2026-09-30
MILESTONE: P16-ARCH-001
TOUCHES:   core/Id.hpp's Id<Tag> contract and IdAllocator, ADR-024 (a selection
           has an identity), ARCHITECTURE.md's "stable typed IDs ... never
           indices" rule
```

## Context

`ARCHITECTURE.md` requires "**Stable typed IDs** for persistent identity — never indices,
kernel handles or addresses", and `core/Id.hpp` implements it with one template used by
sixteen kinds of identifier. Its own documentation states the contract:

> "IDs are identities, never container indices: they stay stable when other objects are added
> or removed, and they are **persisted with the document**."

and `IdAllocator` exists so that "a stale reference can never silently resolve to a newer
item".

P16's invariants say the opposite about mesh entities:

> "Generated mesh IDs are mesh-local identities, not permanent CAD identities."
> "Remeshing may invalidate NodeId / ElementId."
> "Geometry references and mesh identities must remain separate."

So the obvious move — `using NodeId = Id<NodeIdTag>` — is not a small convenience. Set the two
contracts side by side:

```text
                        bettercad::Id<Tag>         a mesh node
stable across rebuild   yes, that is the point     NO -- remeshing invalidates it
persisted               yes                        NO -- never in a file
an index                never                      effectively yes: dense, 0..n-1
allocated by            IdAllocator, never reused   the mesher, reused every run
uniqueness scope        the document                one generation of one mesh
```

A `NodeId` behind that template would inherit a documented promise it breaks on every count,
and would silently *lack* the never-reuse guarantee that is the whole reason a stale CAD
reference is safe in BetterCAD. Two meshes would hand out node 1 for different points.

## Constraints

- Mesh storage is the largest thing in the system. A tetrahedron stores four node references;
  a million-element mesh stores four million. An 8-byte `Id` doubles connectivity memory
  against a 4-byte index for no benefit, and wrecks cache behaviour in the inner loop of every
  solver assembly.
- `ARCHITECTURE.md`'s "never indices" rule must not be weakened, worked around, or quietly
  reinterpreted.
- A stale handle must be *detectable*, not merely documented as invalid — P15 established that
  a dangling reference becomes an explicit unresolved state and never silently rebinds.
- Tet4 is the only element type P16 ships, but the data model must not make Tet10 a rewrite.

## Options

**1. `NodeId = Id<NodeIdTag>`, allocated by an `IdAllocator`.** Uniform with the rest of the
codebase, and false: it promises persistence and stability that a mesh cannot give, costs 8
bytes per reference, and the never-reuse guarantee is meaningless when the whole mesh is thrown
away and rebuilt.

**2. Bare `std::uint32_t` everywhere.** Compact and honest about being an index, but a node
index, an element index and a facet index become the same type, so passing one where another is
wanted compiles. That is precisely the class of defect every strong type in this repository
exists to prevent.

**3. Distinct strong handle types, local to the meshing module, deliberately *not*
`bettercad::Id`.** Compact, mutually incompatible, and named so that the difference from a CAD
identity is visible at the point of use.

## Decision

**Option 3.**

`NodeId` and `ElementId` are **strong index types in the meshing module, and are deliberately
not `bettercad::Id<Tag>`.** They wrap a 32-bit index, do not convert to integers or to each
other, and do not widen to `ObjectId`. `core/Id.hpp` gains no node or element tag. The header
that declares them states why, so the next person does not "fix" the inconsistency.

**"Never indices" is not weakened, because a node has no persistent identity to name.** The
architecture rule governs *persistent identity*: a thing that must still be the same thing after
a regeneration, a save and a load. Nodes and elements are derived state, destroyed and recreated
wholesale by every remesh. There is nothing for a stable ID to be stable about. Naming a node by
its position in the mesh it belongs to is not the failure the rule forbids — it is the correct
representation of an entity whose existence is scoped to one generation of one mesh.

**Scope is carried by the mesh, and a stale handle is detectable.** A mesh carries a `MeshId`
and a **generation counter** that increments on every remesh. A handle is meaningful only with
the mesh it came from, and every public accessor is bounds-checked, so an out-of-range handle is
a structured error and never a read of the wrong node. Anything that stores a handle across a
possible remesh stores the `MeshId` and generation with it, and is refused — not silently
reinterpreted — when they no longer match. This is the same shape as a dangling material
assignment in P15: an explicit unresolved state, never a rebind.

**No handle may cross a persistence or a CAD-reference boundary.** A `NodeId` or `ElementId`
may not appear in a `.bcad` file, in a `MeshControl`, in a command, in a `FaceName`, or in any
API of a module below meshing. Boundary conditions and selections name CAD geometry
(ADR-032), never mesh entities, which is what makes them survive a remesh.

**Tet4 only, and the element type is a tag rather than an assumption.** P16 ships the 4-node
linear tetrahedron and no other element. An element records its type; connectivity is stored as
a variable-length span rather than `array<NodeId, 4>`, so Tet10 is a new enumerator and a new
validator, not a new data model.

**Are Tet4 elements sufficient for the P17 foundation?** For building and qualifying the
pipeline, yes: assembly, boundary conditions, solve, recover, validate against analytic cases.
**For accurate stress in bending, no** — the linear tetrahedron has constant strain, is
excessively stiff in bending, and converges slowly on exactly the stress concentrations
mechanical engineers care about. That is recorded here as a known limitation of the P16/P17
foundation rather than discovered later as a validation failure, and it is why the element type
is a tag from the first day.

## Consequences

- Connectivity is half the size it would be under option 1, and the type system still refuses a
  node handle where an element handle is wanted.
- `core/Id.hpp` stays honest: every `Id<Tag>` in it is still a persistent, never-reused identity.
- A deliberate inconsistency exists — one family of handles in the codebase is not a
  `bettercad::Id` — and is documented at its declaration, because the alternative is a lie in a
  type everything depends on.
- P17 cannot store a node handle in anything durable. It must express a load or a restraint in
  CAD terms. That constrains P17 and is intended.
- Tet4-only results will be conservative in bending. P17's validation cases must be chosen so
  that discretisation error is distinguishable from a solver defect, and its reported accuracy
  must say which is which.

## Validation

```text
P16-DATA-001     a NodeId does not convert to an integer, to an ElementId, or to
                 ObjectId -- compile-fail cases, as P15-UNITS-001 did with 124 of
                 them
P16-DATA-001     an out-of-range handle is a structured error, not a read
P16-GEOM-001     a handle from generation N is refused against generation N+1
P16-PERSIST-001  no node or element handle appears in a saved file
P16-QUAL-001     grep the tree: no mesh handle in any module below meshing
```

## Invariants

```text
NodeId and ElementId are meshing-module strong index types, never bettercad::Id,
and never widen to ObjectId.

A handle is valid only with the MeshId and generation it came from. A mismatch is
refused with a diagnostic and is never reinterpreted.

No mesh handle is persisted, stored in canonical intent, or named in any API
below the meshing module.

A CAD reference never names a mesh entity, and a mesh entity never names itself
to the outside world.

P16 ships Tet4 only, and an element carries its type so that a higher order is an
addition rather than a rewrite.
```
