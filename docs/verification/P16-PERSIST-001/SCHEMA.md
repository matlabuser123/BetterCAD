# P16-PERSIST-001 — the persisted meshing schema

```text
SUBJECT:  every field of the persisted form, its units, and what validates it
FORMAT:   bettercad-document, version 2 (unchanged)
WHERE:    src/io/json/MeshControlJson.cpp
```

## 1. Where it lives, and why there is no new version

A `MeshControl` is an entry in the document's existing `objects` array, with
the same `{id, type, name, data}` envelope every other object kind uses:

```json
{
  "id": 7,
  "type": "mesh-control",
  "name": "PlateMesh",
  "data": { ... }
}
```

**No version bump, and that is the format's own rule**, stated in
`DocumentFile.hpp`: *"adding a kind, an object type or an optional field needs
no bump. Changing what an existing field means needs one."* P15 added
`material` the same way. The version stays **2**, `kOldestReadableDocumentVersion`
stays **1**.

There is no `meshSchemaVersion`, no sidecar file and no second format. One
source of truth.

## 2. The fields

Lengths are **metres**, angles are **radians** — bare SI doubles, written with
`.si()`, which is what every quantity in this format already is. There is no
numeric+unit form and no display-unit intent anywhere in the document, so
`10 mm` is `0.01` and comes back as the same `Length`.

| Field | Canonical? | Unit | Required | Introduced | Absent means | Validated by |
| --- | --- | --- | --- | --- | --- | --- |
| `body` | YES | — (ObjectId) | **yes** | v2 | — | `MeshControl::validate`: must be a valid handle |
| `surface.linear_deflection` | YES | m | **yes** | v2 | — | `validate(SurfaceMeshControls)` via the chain |
| `surface.angular_deflection` | YES | rad | **yes** | v2 | — | as above |
| `sizing.target_size` | YES | m | no | v2 | **BetterCAD's scale-relative default** | `validate(MeshSizingControls)`: positive, finite |
| `sizing.local[].face` | YES | — (`FaceName`) | **yes** | v2 | — | `validate(FaceSelector)` on read, then the sizing validator |
| `sizing.local[].target_size` | YES | m | **yes** | v2 | — | `validate(MeshSizingControls)`: positive, finite, no duplicate face |
| `quality.limits[].metric` | YES | — (string) | **yes** | v2 | — | the name table; unknown is refused by name |
| `quality.limits[].warning` | YES | metric's own | no | v2 | no warning bound | `validate(QualityThresholds)`: finite, correct direction, not `ContextOnly` |
| `quality.limits[].failure` | YES | metric's own | no | v2 | no failure bound | as above |
| `boundary_sets[].id` | YES | — (`BoundarySetId`) | **yes** | v2 | — | `MeshControl::validate`: no duplicate identity |
| `boundary_sets[].name` | YES | — | **yes** | v2 | — | `validate(NamedBoundarySet)`: non-empty |
| `boundary_sets[].faces[]` | YES | — (`FaceName`) | **yes** | v2 | — | `validate(NamedBoundarySet)`: at least one, none twice |

Sections written only when they say something: `sizing.local`,
`quality` and `boundary_sets` are omitted when empty, which is the format's
existing idiom (`copies`, `configurations`). `surface` is always written —
see §4.

## 3. What is NOT in it

```text
DATA                                PERSISTED AS AUTHORITY?
global sizing                       YES
local sizing intent                 YES
face references (FaceName)          YES
boundary-set intent                 YES
quality threshold POLICY            YES

surface triangles                   NO
mesh nodes                          NO
Tet4 connectivity                   NO
boundary facets                     NO
NodeId / ElementId sets             NO
quality REPORT                      NO
geometry-mesh mapping               NO
render buffers                      NO
Netgen tags, handles, parameters    NO
```

**Structural, not a policy.** A `MeshControlDefinition` has no member for any
of them, and `VolumeMesh` and `GeometryMeshMap` cannot even be
default-constructed (ADR-030) — so there is nothing in the serializer to
exclude. The check is nonetheless made against the written bytes rather than
argued from the code: see `ROUND_TRIP.md` §3.

**No mesh cache.** NOT IMPLEMENTED, and deliberately: there is no measured
performance requirement, the brief forbids introducing one without it, and
nothing in the tree caches a mesh to disk today.

## 4. Four decisions, stated rather than left implicit

**An absent `target_size` is a state, not a missing number.**
`std::optional<Length>` empty means "BetterCAD's scale-relative default"
(P16-SIZE-001). Writing a number for it would replace a canonical state with
whatever this build computes, so it is omitted — and must come back absent.
Tested both ways.

**The deflections are always written.** They always have a value, so there is
no absent state to preserve, and an engineering document is better off
carrying the numbers it was meshed with than reconstructing them from whatever
a later build's defaults are. This is §83's default-drift hazard answered by
writing the values explicitly instead of relying on a version-dependent
default.

**`quality.limits` is an array, not an object keyed by metric.** A JSON object
with the same key twice silently keeps one — the kind of quiet loss this
format refuses everywhere else. An array makes a repeated metric visible, and
it is refused by name.

**Enumerations are strings from an explicit table, never integers and never
the diagnostic text.** An integer would silently become a different metric the
day an enumerator is inserted in the middle; `toString(QualityMetric)` is
display text a later milestone may reword. The same reasoning P15's material
mapping records, and the same reasoning behind `kFaceRoles`.

Of the 18 quality metrics, **7 can carry a threshold**: four
higher-is-better (`tet_radius_ratio`, `tet_min_dihedral_angle`,
`triangle_shape_quality`, `triangle_min_angle`) and three lower-is-better
(`tet_aspect_ratio`, `tet_max_dihedral_angle`, `triangle_max_angle`). The
other 11 are dimensioned sizes that P16-QUALITY-001 classifies `ContextOnly`,
and `validate(QualityThresholds)` refuses a bound on them — so such a
threshold cannot exist in a document, let alone in a file. Asserted both ways
rather than assumed.

## 5. Validation is the core's

The parser checks **syntax, type, required fields, unknown fields and
duplicate metrics**. Everything else is the core's, because reading ends in
`MeshControl::create`:

```text
parse  ->  MeshControlDefinition  ->  MeshControl::create
                                        validate(MeshSizingControls)
                                        validate(QualityThresholds)
                                        validate(NamedBoundarySet)
                                        no two sets sharing an identity
```

So a file carrying a zero, negative or non-finite size, two local controls on
one face, or two sets with one identity is refused by **exactly the rules the
API enforces**, and there is no sizing arithmetic in `MeshControlJson.cpp` to
drift from them. That is the whole of the brief's §28, §29, §66 and §74, and
it is why the malformed-input matrix needed no new validation code.

## 6. Unknown fields are rejected

`requireObject` takes an allowed-key list at every level, so
`"netgen_maxh": 3` inside a sizing section fails with
`meshing...sizing.netgen_maxh: unknown field`. This is the format's existing
strictness and P16 is not made uniquely lenient. The consequence for forward
compatibility is deliberate: an older build meeting a newer file **refuses and
names what it did not recognise**, rather than skipping a section and silently
losing engineering intent.

## 7. The hand-written reference

`examples/models/meshed_plate.bcad` is written **by hand**, as
`plate.bcad` is and for the reason that file records: the format should be
readable and editable without BetterCAD, and the example serves the CLI
process tests.

For this milestone it carries more weight. A person wrote every field of the
meshing section with no mesher present, and BetterCAD interprets all of it —
which is the backend-independence claim demonstrated rather than asserted.
Nothing in the file names Netgen, `maxh`, `grading` or any backend concept,
because the canonical schema is BetterCAD's and the mapping to a backend is an
implementation detail a future backend must be free to change.
