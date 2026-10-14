# P17-PERSIST-001 — the schema

```text
SUBJECT:  every field a structural analysis persists, every field it does not,
          and the version decision
FORMAT:   .bcad, transparent JSON, unchanged
VERSION:  2 -- NOT bumped. See "The version" below.
```

## Where it lives

One new object type in the existing document format. Not a second file, not a
linked subdocument, not an `analysis.p17fea`.

```text
Document (.bcad, "version": 2)
├─ parameters
├─ material_assignment          P15
└─ objects
   ├─ sketch, extrude, ...      P0-P12
   ├─ material                  P15
   ├─ mesh-control              P16
   └─ structural-analysis       P17   <- the only thing this milestone adds
```

The reader and writer are `src/io/json/StructuralAnalysisJson.cpp`, declared in
`ObjectJson.hpp` beside every other object kind's pair, and dispatched in
`DocumentJson.cpp` in the two places every other kind is.

## The schema

```json
{
  "id": 9,
  "type": "structural-analysis",
  "name": "PlateStudy",
  "data": {
    "mesh": 7,
    "mode": "linear_static",
    "loads": [
      {
        "id": 1,
        "type": "pressure",
        "face": { "feature": 6, "face": { "role": "end_cap" } },
        "magnitude": 2500000.0
      },
      {
        "id": 2,
        "type": "surface_traction",
        "face": { "feature": 6, "face": { "role": "side", "entity": 5 } },
        "traction": [ 1234.5, -2500.0, 0.0 ]
      },
      {
        "id": 3,
        "type": "gravity",
        "acceleration": [ 0.0, 0.0, -9.80665 ]
      }
    ],
    "restraints": [
      {
        "id": 1,
        "face": { "feature": 6, "face": { "role": "start_cap" } },
        "components": [ "ux", "uy", "uz" ]
      }
    ],
    "solver": {
      "algorithm": "simplicial_ldlt",
      "relative_residual_tolerance": 1e-08,
      "pivot_floor": 1e-12
    }
  }
}
```

`examples/models/analysed_plate.bcad` is this, hand-written, and two tests read
it: one checks every field against the values as written, the other regenerates
the geometry, meshes it and solves it.

## Field by field

| Key | Type | Unit | Required | Notes |
| --- | --- | --- | --- | --- |
| `mesh` | id | — | yes | the `MeshControlId` analysed. Names the body transitively |
| `mode` | string | — | yes | `linear_static` \| `linear_static_with_gravity` |
| `loads` | array | — | no | **omitted when empty.** The definition's own order |
| `loads[].id` | id | — | yes | `LoadId`, unique within this analysis |
| `loads[].type` | string | — | yes | `surface_traction` \| `pressure` \| `gravity` |
| `loads[].face` | face ref | — | for the two face kinds | `faceNameToJson`, shared with every other kind |
| `loads[].traction` | [x,y,z] | Pa | traction only | **global** axes, never surface-local |
| `loads[].magnitude` | number | Pa | pressure only | **signed scalar**, never a frozen vector |
| `loads[].acceleration` | [x,y,z] | m/s² | gravity only | the acceleration, never ρ and never ρg |
| `restraints` | array | — | no | omitted when empty. The definition's own order |
| `restraints[].id` | id | — | yes | `RestraintId`, unique within this analysis |
| `restraints[].face` | face ref | — | yes | the one canonical target kind |
| `restraints[].components` | array | — | yes | `ux`/`uy`/`uz`, in that order, non-empty |
| `solver.algorithm` | string | — | yes | `simplicial_ldlt` |
| `solver.relative_residual_tolerance` | number | — | yes | dimensionless |
| `solver.pivot_floor` | number | — | yes | dimensionless |

All numbers are bare SI doubles, as everywhere else in the format. No display
unit, no unit tag, no formatted string.

## Six decisions, and why each one

**Enum keys are strings, not integers, and not the display text.** P16's
quality metrics settled both halves: an integer silently becomes a different
value the day an enumerator is inserted in the middle, and `toString()` is
display text a later milestone may reword or translate. A file key must outlive
both.

**Collections are arrays, not objects keyed by id.** A JSON object with a
repeated key silently keeps one of them. An array makes a repeated `LoadId`
visible, and the reader refuses it by name. The same reason P16 gives for its
`limits` array.

**The solver section is always written in full.** An engineering document is
better off carrying the numbers it was solved with than reconstructing them
from whatever a future build's defaults happen to be — P16's reason for always
writing both surface deflections. It also removes a class of silent test pass:
a dropped field cannot hide behind a default that happens to match.

**An empty collection is omitted, not written as `[]`.** P16's convention,
frozen here so the determinism claim has a fixed shape. A hand-written `[]` is
accepted and normalises away on the first save; see
[DETERMINISM.md](DETERMINISM.md).

**The load and restraint order is the user's and is preserved.** This is the
opposite of P16, which sorts its local sizing controls — and both are right for
their own reason. P16-SIZE-001 declares that order meaningless;
`StructuralAnalysisDefinition` declares the opposite in as many words, over
both vectors, and P17-CMD-001's probe C8 exists because an edit that silently
reordered the loads was a real defect. Determinism is unaffected, because here
the order *is* part of the canonical state.

**The component mask is an array of names.** `7` meaning "fixed" needs a reader
to know the bit order. `["ux","uy","uz"]` does not, and a renumbered
`DofComponent` cannot change what an old file means. The canonical order is
free rather than enforced: `RestraintComponents` is a bitmask with no insertion
order to lose, so iterating the name table is the only order it can produce.

## The version

**`kDocumentFormatVersion` stays at 2.** `DocumentFile.hpp` carries the rule as
a forty-line comment (P13-PERSIST-001):

> **Bump this only for a change that an existing reader would get WRONG.**

Adding an object type is additive, and both directions already fail safely
without the version doing any work:

```text
new reader, old file   loads. An absent section means "none of those", which
                       is what an older document meant by not having one
old reader, new file   REFUSES, by name -- "unknown object type
                       'structural-analysis'" -- never a silent skip, so no
                       data is lost to a reader that predates P17
```

The same comment makes the point against itself: the schema has grown
parameters, expressions, configurations, datums, stable face references,
components, mates, materials and meshing controls, "and the number has
correctly never moved, because every one of those was additive." A P17 bump
would be the first unnecessary one.

## What is NOT in the schema

See [AUTHORITY.md](AUTHORITY.md) for the full table and the measurement behind
it. In one line: the exclusion is **structural**, not a discipline this file
maintains. `StructuralAnalysisDefinition` has five members and none of them is
derived, and a `StructuralResult` does not live on a document object at all —
it is returned by `solveStructuralAnalysis` and held by the caller. There is
nothing here to leave out.
