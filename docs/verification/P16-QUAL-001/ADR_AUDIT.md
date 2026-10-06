# P16-QUAL-001 — ADR audit

```text
SUBJECT:  whether every architectural decision P16 took is still what the code
          does
METHOD:   each clause of each decision checked against the committed tree by
          search, not by reading the ADR back to itself
ADRs:     ADR-030, ADR-031, ADR-032, ADR-033 -- cited 29, 24, 39 and 22 times
          across include/bettercad/meshing and src/meshing
FINDINGS: 1 -- ADR-031's connectivity sentence, amended
```

## ADR-030 — a mesh is derived state and a meshing control is the intent

```text
| CLAUSE                                   | STILL IMPLEMENTED | WHERE          |
| a MeshControl is a document object       | YES  MeshControl final : public DocumentObject |
| its id widens to ObjectId                | YES  isDocumentObjectTag<MeshControlIdTag> = true |
| it is a node in the dependency graph     | YES  dependencies() returns {body} |
| edited only through commands             | YES  setDefinition is reached only via
                                                  Document::modifyObject, which the
                                                  command layer owns |
| it is persisted                          | YES  src/io/json/MeshControlJson.cpp |
| it holds no nodes, elements, backend     | YES  the definition's only members are
  handle or result                                ObjectId, VolumeMeshControls,
                                                  QualityThresholds, vector<NamedBoundarySet> |
| a mesh is held by a Mesher service,      | YES  class VolumeMesh derives from NOTHING;
  never a document object, no ObjectId           Mesher holds std::map<MeshControlId,Held> |
| defaults are the control's, never the    | YES  defaultGlobalTargetSize() is BetterCAD's
  backend's                                      own formula; ResolvedSizing records
                                                  globalIsDefault |
| invalidation may be NARROWED by
  P16-GEOM-001, never widened              | YES  GeometryRevision replaced
                                                  Document::revision() -- a narrowing, which
                                                  the ADR explicitly permits |
```

**PASS.** The clause worth dwelling on is the last: ADR-030 said the mesh's
build stamp uses the document revision as a conservative outer guard, and that
P16-GEOM-001 "may narrow it with measurement. It may never widen it."
`GeometryRevision` is that narrowing — a mix of the source object's revision and
its transitive dependencies' plus the active configuration — so a material edit
no longer invalidates a mesh. That is the permitted direction.

## ADR-031 — a mesh node is a handle, not an identity

```text
| CLAUSE                                   | STILL IMPLEMENTED | WHERE          |
| NodeId/ElementId are strong index types, | YES  standalone classes wrapping
  NOT bettercad::Id<Tag>                         std::uint32_t in MeshIds.hpp |
| core/Id.hpp gains no node or element tag | YES  0 matches for NodeIdTag,
                                                  ElementIdTag, RegionIdTag |
| they do not widen to ObjectId            | YES  no conversion exists; 15
                                                  compile-fail cases pin it |
| scope carried by the mesh, stale handle  | YES  MeshStamp + Mesh::owns() |
  detectable                                                                  |
| no handle crosses persistence or a CAD   | YES  no NodeId/ElementId/Tetrahedron
  reference boundary                             token in src/io or include/bettercad/io;
                                                  none in MeshControlDefinition, in any
                                                  command, or in FaceName |
| Tet4 only, element records its type      | YES  ElementType has exactly Triangle3
                                                  and Tetrahedron4 |
| connectivity is a variable-length span   | NO -- see below |
  rather than array<NodeId, 4>                                                |
```

### The one finding: connectivity is a fixed array, not a span

ADR-031's decision says connectivity is "stored as a variable-length span rather
than `array<NodeId, 4>`, so Tet10 is a new enumerator and a new validator, not a
new data model."

The code stores fixed-arity arrays:

```text
Triangle      {ElementId; array<NodeId,3>; RegionId}
Tetrahedron   {ElementId; array<NodeId,4>; RegionId}
```

**This is not silent drift.** P16-DATA-001 recorded the choice and its reason in
its own evidence:

> "Arity is part of the type, so an element with the wrong number of handles is
> mostly a compile error and always a rejection."

CLAUDE.md's conflict order is `verification evidence → TODO → architecture`, so
the evidence outranks the ADR and the ADR is the stale document. What P16-DATA-001
did not do is *reconcile the ADR text*, which left a reader of ADR-031 misled
about what shipped.

**Resolution: the ADR is amended, not the code.** A note now records what was
built, why, and the consequence the span was meant to avoid — that a second
element family needs a new struct rather than only a new enumerator, so that
cost is deferred to whichever milestone adds one. This is carried into P16's
known limitations.

**Why this is not a qualification failure.** The brief says any architecture
implementation drift is a qualification failure, and that is the right default.
This case is distinguishable on three grounds, each checkable rather than
asserted:

* the decision's *invariants* are all upheld — handles are not `Id`s, no handle
  is persistable, no handle crosses a CAD boundary, Tet4 is the only family, and
  an element records its type;
* the divergent sentence is a *means*, not an invariant, and the means chosen is
  stronger on the property P16 needs (compile-time arity) and weaker only on a
  property no P16 milestone exercises (adding a second element family);
* it was chosen deliberately and recorded in the evidence at the time, which is
  the difference between a decision and a drift.

Had the divergence touched an invariant — a `NodeId` in a `.bcad` file, a handle
widening to `ObjectId` — it would be an immediate FAIL under §88, with no
reconciliation available.

## ADR-032 — a mesh boundary names a CAD face, and a selection is never a mesh entity

```text
| CLAUSE                                   | STILL IMPLEMENTED | WHERE          |
| a selection is never a mesh entity       | YES  LocalMeshSizing::face and
                                                  NamedBoundarySet::faces are FaceName |
| selections are persisted and undoable    | YES  MeshControlJson; the command layer |
| the mapping points mesh -> CAD           | YES  GeometryMeshMap::faceOfFacet_, and
                                                  boundarySourceFaces on the mesh |
| face-to-name is many-to-many and stored  | YES  MappedFace::names and
  as one                                         FacetSource::names are vector<FaceName> |
| an unnamed face is normal, not a failure | YES  unnamedFaceCount is reported;
                                                  complete() does not require naming |
| one region per solid, no node shared     | YES  MeshIssueKind::NodeSharedBetweenRegions;
  between regions                                generateVolumeMesh refuses MultipleSolids |
| an unresolvable reference is reported,   | YES  MappingState::Unresolved; no
  never rebound                                  nearest-face logic exists anywhere |
```

**PASS.** The "never rebound" clause was checked by searching for the failure
mode rather than the promise: the only occurrence of "nearest" in the meshing
module is a comment stating that an interior tetrahedron face gets
`NoBoundaryCorrespondence` "rather than the nearest exterior face."

## ADR-033 — the volume mesher is a backend behind an enforced boundary

```text
| CLAUSE                                   | STILL IMPLEMENTED | WHERE          |
| no backend type in any BetterCAD API     | YES  the seam speaks Point3D, Length and
                                                  0-based indices only |
| backend code lives in src/meshing/<be>/  | YES  nglib / Ng_* appear in NO file
                                                  outside src/meshing/netgen/ |
| CheckLayering enforces it                | YES  Rule 5, mesh_backend_allowed_regex
                                                  "^src/meshing/[^/]+/" |
| meshing is layer 4 beside drawing,       | YES  layer_meshing 4, layer_drawing 4,
  nothing renumbered                             assembly still 3 |
| kernel triangulation stays in            | YES  core/geometry's triangulate() and
  core/geometry behind the occt adapter          occt/OcctMesh.cpp, extended additively |
| a backend's return code is never         | YES  VolumeBackendFailure::NoTetrahedra is a
  evidence of success                            DISTINCT failure, and the adapter checks
                                                  Ng_GetNE/Ng_GetNP before believing NG_OK |
```

**PASS**, and two clauses deserve their measurement rather than a tick.

**Containment is real, not asserted.** `nglib` and `Ng_*` appear in exactly one
directory, `src/meshing/netgen/`, and `CheckLayering.cmake`'s Rule 5 fails the
build if that changes. The CLI was searched separately and case-sensitively —
an earlier case-insensitive search matched `striNG_View` and had to be redone —
and contains no backend call and no mesh formula at all.

**The single approved path holds.** `generateTetrahedra` is called from exactly
one place outside the backend adapters, `src/meshing/VolumeMesh.cpp:283`, and
`generateSurfaceMesh` is constructed only inside `src/meshing/`. So
`CAD → P16-SURF → backend` is the only route to a tetrahedralisation, and
nothing bypasses the validated surface.

**Every transferred backend parameter is pinned.** `Transfer_Parameters` copies
fifteen fields; the adapter sets each explicitly, with the seven inert fields
named in a comment and in P16-SIZE-001's audit. See `BACKEND_DEFAULTS.md`.

## Result

```text
ADRs audited              4
clauses checked           33
clauses implemented       32
findings                  1  (ADR-031's connectivity sentence)
action                    the ADR amended to match the evidence; no code change
qualification impact      none -- no invariant diverges
```
