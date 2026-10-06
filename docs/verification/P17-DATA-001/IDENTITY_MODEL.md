# P17-DATA-001 — the identity model

```text
SUBJECT:  three identity domains, what belongs in each, and why mixing them is
          a compile error rather than a review note
METHOD:   the existing ID system audited before anything was added; every
          mechanism below is the one already in the tree
```

## The audit, before any ID was defined

`include/bettercad/core/Id.hpp` holds one mechanism:

```cpp
template <typename Tag, typename Value = std::uint64_t> class Id;
```

Strongly typed, defaulted `==` and `<=>`, invalid when default-constructed,
`fromValue` the only way in. Two properties decided everything that follows:

```text
isDocumentObjectTag<Tag>   a tag opted into this list widens IMPLICITLY to
                           ObjectId. Sketch, feature, parameter, body,
                           component, mate, sheet, view, dimension,
                           annotation, material and mesh control are in it
NOT in that list           EntityId, ConstraintId, ChamferEdgeId,
                           BoundarySetId. These identify a MEMBER of
                           something, not a document object
```

`BoundarySetId` is the precedent P17 follows for loads and restraints, and its
own documentation says why: a boundary set is "unique within whatever owns the
sets, as a chamfer's edge selections are unique within their chamfer", and its
identity is the intent while "the facets, nodes and elements are derived afresh
for whatever mesh is current".

The allocation convention came from the same place: `AddBoundarySetCommand`
takes the `NamedBoundarySet` with its ID already chosen and **refuses a
duplicate**. The caller picks, the command checks. That is what makes undo
exact — the same ID comes back — and P17's loads and restraints inherit it
rather than inventing an allocator.

## The three domains

| Identity | Domain | Canonical | Persisted | Lifetime | Survives remesh | Owner |
| --- | --- | --- | --- | --- | --- | --- |
| `AnalysisId` | Document | Yes | Yes | The document's | **Yes** | `structural` |
| `LoadId` | Document, member of an analysis | Yes | Yes | Its analysis's | **Yes** | `structural` |
| `RestraintId` | Document, member of an analysis | Yes | Yes | Its analysis's | **Yes** | `structural` |
| `LoadCaseId` | — | — | — | — | — | **not required**, see below |
| `FaceName` | CAD intent | Yes | Yes | The feature's | Yes, by resolution | `features` |
| `NodeId` | One mesh | No | No | That mesh's | **No** | `meshing` |
| `ElementId` | One mesh | No | No | That mesh's | **No** | `meshing` |
| `DofIndex` | One numbering of one mesh | No | **Never** | One solver preparation | **No** | `structural` |
| `MeshStamp` | One mesh generation | No | No | That mesh's | **No** | `meshing` |
| Result provenance | Derived | No | No | Its result's | No | `structural` |

### What distinguishes a document identity, in code

```cpp
static_assert(std::is_convertible_v<AnalysisId, ObjectId>);      // widens
static_assert(!std::is_convertible_v<LoadId, ObjectId>);         // does not
static_assert(!std::is_convertible_v<RestraintId, ObjectId>);    // does not
static_assert(!std::is_convertible_v<BoundarySetId, ObjectId>);  // the precedent
```

An analysis **is** a document object — `StructuralAnalysis : DocumentObject`,
built this milestone — so its ID widens and the document's own machinery can
take it. A load and a restraint are members of that object's definition, so
theirs must not: a `LoadId` that widened could be handed to `findObject`, to the
dependency graph or to a command expecting an object, and would silently
resolve to whatever object happens to share its number.

### Why `DofIndex` is not in `core/Id.hpp`

Because that header is where **persisted** identity lives, and a DOF index is
the opposite of persisted:

```text
NodeId        meaningful for as long as the mesh is
DofIndex      meaningful for as long as the NUMBERING is -- renumber the same
              mesh and it means something else
```

The failure it prevents is concrete. A restraint that stored "DOF 1042" would,
after a remesh, constrain whatever material happened to land at equation 1042 —
a different face, silently, with a plausible-looking answer. A restraint that
stores a CAD reference resolves correctly to new nodes and a new numbering.

It is also **64-bit while a `NodeId` is 32-bit**, and that is not decoration:
there are three DOFs per node, so a 32-bit index overflows before the node count
does.

```cpp
static_assert(std::is_same_v<DofIndex::ValueType, std::uint64_t>);
static_assert(sizeof(DofIndex::ValueType) > sizeof(meshing::NodeId::ValueType));
```

`NodalDof{NodeId, DofComponent}` is the durable way to name a degree of
freedom — meaningful for as long as the mesh is. `DofIndex` is the solver's
numbering of it. Keeping them different types is what stops "the z of this
node" becoming "equation 1042".

## `LoadCaseId`: NOT REQUIRED, and why

**Decided, not skipped.** The authorized initial scope (ADR-034) is:

```text
one analysis  ->  one set of loads and restraints  ->  one linear solve
```

There is no load-case semantics for the ID to identify: no combination, no
envelope, no independently defined case within an analysis. One analysis **is**
one load case.

Adding the type anyway would leave a strongly-named identity that nothing
allocates, nothing stores and nothing resolves — which a later developer would
reasonably read as working infrastructure. The brief names this risk and this
milestone declines it. `grep -rn LoadCaseId include/ src/ tests/` returns **0**.

When multiple load cases are authorized, the ID is one tag and one alias beside
`LoadId`, and the analysis grows a collection. Nothing in this model has to
change to allow it.

## Mesh handles inside a result: permitted, and the nuance

ADR-031 says a mesh handle is not an identity. A `StructuralResult` nonetheless
keys its arrays on the mesh's own enumeration, and that is consistent:

```text
PERMITTED   derived arrays belonging to ONE mesh, discarded with it. The result
            records the MeshStamp, so it can refuse to be read against another
FORBIDDEN   a handle in a .bcad file, in an analysis definition, in a command,
            in a FaceName, or as the identity of a load or a restraint
```

After a remesh the same intent resolves to new handles and the old result is
stale. **Numeric IDs are never reinterpreted against a new mesh** — proved by
`StructuralData_AResultIsBoundToTheMeshItWasComputedOn`, which remeshes the same
body to the same node count and watches the result be refused on the stamp.

## Verified

Twelve compile-fail cases, each a hand-written wrong program that compiles only
if the model is broken, plus the control that must compile:

```text
control                          AnalysisId -> ObjectId COMPILES (the one
                                 permitted widening)
analysis-id-as-load-id           REFUSED
load-id-as-restraint-id          REFUSED   <- the copy-paste mistake
restraint-id-as-load-id          REFUSED
load-id-as-object-id             REFUSED   <- a member must not widen
restraint-id-as-object-id        REFUSED
dof-index-as-object-id           REFUSED   <- THE CENTRAL ONE
dof-index-as-node-id             REFUSED
node-id-as-dof-index             REFUSED
dof-index-as-integer             REFUSED
integer-as-load-id               REFUSED
node-id-as-load-id               REFUSED   <- ADR-032, one layer up
node-id-as-restraint-id          REFUSED
```

```text
ctest -R 'compile_fail\.structids' -N      Total Tests: 12
ctest -R 'compile_fail\.structids'         100% passed out of 12
```

The control build is what makes each failure attributable: it compiles the same
file with no macro, so a failure can only come from the selected line. It caught
a real defect during this milestone — an unused helper function, rejected by
`-Werror=unused-function`, which would have made every case fail for the wrong
reason.
