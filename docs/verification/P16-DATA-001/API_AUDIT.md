# P16-DATA-001 — public API audit

What a consumer of the mesh data model is forced to include, and what identities it can
confuse with what. Checked against the tree rather than asserted.

## 1. No OCCT type is reachable from the public meshing API

The complete BetterCAD include closure of `include/bettercad/meshing/*.hpp`:

```text
bettercad/core/Error.hpp          Result, Error, ErrorCode
bettercad/core/math/Point.hpp     Point3D
bettercad/core/units/Units.hpp    Length, Area, Volume
bettercad/meshing/Export.hpp      generated
```

plus the standard library: `<array> <compare> <cstddef> <cstdint> <format> <functional>
<optional> <ostream> <span> <string> <string_view> <vector>`.

```text
grep -rnE "TopoDS_|gp_[A-Z]|Poly_|BRep[A-Z]|Standard_" over the meshing headers
AND over every BetterCAD header they include   ->   no match
```

So P17 can consume a mesh without OCCT on its include path. ADR-033's containment is not
merely satisfied — the module has no `occt/` directory at all, because P16-DATA generates
nothing and therefore needs no kernel.

`geometry::triangulate()` is **not** called, referenced, or converted to. The engineering
mesh and the kernel's surface triangulation stay unconnected until `P16-SURF-001` builds the
bridge deliberately.

## 2. No backend identity anywhere

```text
grep -rniE "gmsh|netgen|tetgen|mmg|cgal" over include/bettercad/meshing/ and src/meshing/
->   no match
```

There is no constructor, factory or field through which a backend's own numbering could
become a `NodeId`. The only ways to obtain one are `MeshBuilder::addNode`, which allocates,
and `NodeId::fromValue`, which is explicit and named — and `INTEGER_TO_NODE_ID` proves a bare
integer cannot become one implicitly.

## 3. The identity separation, as the compiler enforces it

Fifteen compile-fail cases, each with a control target that must compile, so every failure is
attributable to its own line rather than to a missing header.

| Case | Proves |
| --- | --- |
| `node-id-as-element-id` | a node is not an element |
| `element-id-as-node-id` | and not the reverse |
| `region-id-as-node-id`, `node-id-as-region-id` | a region is neither |
| **`node-id-as-object-id`** | **a mesh handle never widens to a CAD identity** |
| **`element-id-as-object-id`**, **`region-id-as-object-id`** | nor do the other two |
| `object-id-as-node-id`, `sketch-id-as-node-id` | a document object is not a node |
| `integer-to-node-id` | identity never comes from a bare number |
| `node-id-to-integer` | and never decays back to one |
| `compare-node-and-element` | the two do not even compare |
| `mutate-node-through-mesh` | a consumer cannot move a node |
| `add-element-through-mesh` | a consumer cannot change topology |
| `triangle-with-tet-connectivity` | arity is part of the type |

The three `*-as-object-id` cases are the ones that matter most: they are ADR-031's central
invariant, and without them "a mesh handle is not a CAD identity" would be a sentence in a
document.

### One case that was removed because it could not fail

`addTetrahedron({n1, n2, n3}, region)` — a braced list with too **few** elements — is valid
aggregate initialisation of `std::array<NodeId, 4>` and zero-fills the fourth. It is not a
compile error and cannot be made one. Keeping it would have produced a test that passed for
an unrelated reason, which is the failure mode compile-fail suites exist to avoid. It was
replaced by a runtime test,
`MeshBuilder_RejectsAnElementWithAnUnderfilledConnectivityList`, which pins that the zero
handle it produces is refused. Too **many** elements is a compile error and is the case that
remains.

### One diagnostic regex that was wrong

`triangle-with-tet-connectivity` first asked for `could not convert`; GCC 16.1.0 emits
`cannot convert '<brace-enclosed initializer list>' to 'const std::array<...NodeId, 3>&'`.
The case failed to compile exactly as intended, and the harness correctly refused to count it
until the regex named the real diagnostic. That check is the difference between "the build
failed" and "the build failed for the reason claimed".

## 4. Mutability

```text
Mesh::nodes()        std::span<const Node>
Mesh::triangles()    std::span<const Triangle>
Mesh::tetrahedra()   std::span<const Tetrahedron>
Mesh::regions()      std::span<const RegionId>
```

Every accessor is `const` and hands back a view of `const`. No method on `Mesh` mutates
anything, and no reference to the underlying containers escapes. `MeshBuilder::build()`
returns by value, so a finished mesh cannot be changed through the builder that made it.

P17 therefore holds a `const Mesh&` and has nothing to call that would move a node or alter
connectivity — which is P16's solver-facing requirement, enforced rather than documented.

## 5. What is deliberately absent

```text
ValidatedMesh        ADR-030 gives it to the mesher's VALIDATING path, and naming a
                     data-level check "solver ready" is the confusion this milestone
                     is supposed to prevent. The report method is dataValid().
FaceName / ObjectId  geometry correspondence is P16-MAP-001's. A node lying on a CAD
                     face does not make the face part of the node.
material             ADR-028 and ADR-032: no mesh, region or element holds a property.
adjacency            derived, and nothing needs it yet. The one global check that
                     could have wanted it -- no node shared between regions -- is
                     computed locally inside validate() and exposes no structure.
mesh staleness       ADR-030 gives revisions and invalidation to the mesher.
persistence          P16-PERSIST-001. Nothing here serialises, and the data model is
                     ready for it: no raw pointers, no iterator identity, no
                     address-based references.
```
