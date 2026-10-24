# P18-PERSIST-001 — the schema

```text
ONE OBJECT TYPE:   "thermal-analysis"       in the existing .bcad document, document version 2 (unchanged)
UNITS:             fixed SI, named by the key; no unit string, no Celsius, no millimetre
UNKNOWN KEY:       rejected, by name, at every level
FIRST VERSION:     the first BetterCAD document that can contain P18 thermal intent is version 2 — there is no earlier P18 schema
```

## An object, as written

The analysed fixture, `examples/models/thermal_plate.bcad` (typed by hand, not by the serializer), ends:

```json
{
  "id": 9,
  "type": "thermal-analysis",
  "name": "PlateThermalStudy",
  "data": {
    "mesh": 7,
    "mode": "transient",
    "loads": [
      { "id": 1, "type": "surface_heat_flux", "face": { "feature": 6, "face": { "role": "end_cap" } }, "flux": -1234.5 },
      { "id": 2, "type": "volumetric_heat_source", "generation": 67890.25 }
    ],
    "boundary_conditions": [
      { "id": 1, "type": "prescribed_temperature", "face": { "feature": 6, "face": { "role": "start_cap" } }, "temperature": 345.125 },
      { "id": 2, "type": "convection", "face": { "feature": 6, "face": { "role": "side", "entity": 5 } }, "coefficient": 27.75, "ambient": 291.625 }
    ],
    "initial_temperature": 312.875,
    "solver": { "relative_residual_tolerance": 1e-08, "pivot_floor": 1e-12 },
    "transient": { "time_step": 0.125, "end_time": 2.375, "output": "every_step" }
  }
}
```

(Shown compacted; the file is written with the format's 2-space indentation and is compared character for character by `TheHandWrittenFileIsExactlyWhatTheWriterProduces`.)

## Every persisted field

| Key | Canonical field | Unit | When present | Validated by |
| --- | --- | --- | --- | --- |
| `object.id` | `ThermalAnalysisId` (the document's object id) | — | always | the document |
| `object.name` | the object's name | — | always | the document (a valid identifier) |
| `data.mesh` | `ThermalAnalysisDefinition::mesh` (`MeshControlId`) | — | always | `readId`; `ThermalAnalysis::create` (valid handle) |
| `data.mode` | `mode` | `steady` \| `transient` | always | enumeration table |
| `data.loads[]` | `loads`, **in the user's order** | | omitted when empty | |
| `.id` | `ThermalLoadId` | — | each | `readId` (cap 2^53-1, not 0); unique within the analysis |
| `.type` | `ThermalLoadKind` | `surface_heat_flux` \| `volumetric_heat_source` | each | enumeration table |
| `.face` | `SurfaceHeatFluxLoad::face` (`FaceName`) | — | surface flux only | `faceNameFromJson`; **a valid feature handle** |
| `.flux` | `SurfaceHeatFluxLoad::flux` | **W/m²**, signed | surface flux only | `validate(ThermalLoad)` (finite) |
| `.generation` | `VolumetricHeatSourceLoad::generation` | **W/m³**, signed | volumetric only | `validate(ThermalLoad)` (finite) |
| `data.boundary_conditions[]` | `boundaryConditions`, in the user's order | | omitted when empty | |
| `.id` | `ThermalBoundaryConditionId` | — | each | as for a load |
| `.type` | `ThermalBoundaryConditionKind` | `prescribed_temperature` \| `convection` | each | enumeration table |
| `.face` | the condition's `FaceName` | — | each | as for a load |
| `.temperature` | `PrescribedTemperature::value` | **K**, absolute | prescribed only | `validate(ThermalBoundaryCondition)` (finite, ≥ 0 K) |
| `.coefficient` | `ConvectionBoundary::coefficient` | **W/(m² K)** | convection only | `validate(...)` (finite, **> 0**) |
| `.ambient` | `ConvectionBoundary::ambient` | **K**, absolute | convection only | `validate(...)` (finite, ≥ 0 K) |
| `data.initial_temperature` | `initialCondition` (`UniformInitialTemperature`) | **K**, absolute | transient only | `validate(UniformInitialTemperature)`; the mode-complete rule |
| `data.solver.relative_residual_tolerance` | `ThermalSolverSettings::relativeResidualTolerance` | ratio | **always written, required on read** | `validate(ThermalSolverSettings)` (finite, > 0) |
| `data.solver.pivot_floor` | `pivotFloor` | ratio | **always written, required on read** | same |
| `data.transient.time_step` | `TransientSettings::timeStep` | **s** | transient only | `validate(TransientSettings)` (0 < step ≤ end, finite) |
| `data.transient.end_time` | `endTime` | **s** | transient only | same |
| `data.transient.output` | `TransientOutputPolicy` | `final_only` \| `every_step` | transient only | enumeration table |

`ThermalAnalysisDefinition` has **seven** members — `mesh`, `mode`, `loads`, `boundaryConditions`, `initialCondition`, `solver`, `transient` — and the table covers all seven; the mirror assertion `ThermalData_TheDefinitionHoldsOnlyIntent` breaks the build if an eighth is added, which forces the author to look at this file; and the whole-definition `operator==` check in `TheWholeSteadyCanonicalIntentRoundTrips` fails if a new member is added and not serialized.

## Seven schema decisions

1. **Variants by an explicit `type` string**, with the keys a kind must **not** carry refused by name ("a volumetric heat source load does not carry this"). `requireObject` allows the union of keys, so without this a volumetric source given a face would pass with the face silently ignored.
2. **Enumerations by stable strings**, not integers and not `toString()`: an integer becomes another mode the day an enumerator is inserted, and a display string is text a later milestone may reword. A table-size check cannot catch a *wrong* entry, so `EveryEnumeratorRoundTrips` puts each enumerator on an analysis in turn.
3. **Solver settings are always written, and all required on read.** An engineering document should carry the numbers it was solved with rather than reconstructing them from a future build's defaults, and a missing field is then a malformed file, not "whatever the default is". A dropped field cannot hide behind a default that happens to match.
4. **Empty collections are omitted**, and an explicit empty array is accepted and normalised away by the first save (`AnExplicitEmptyCollectionNormalisesThenIsByteStable`).
5. **Time is three fields**, not a schedule: the actual step sequence, including a shortened last step, is derived by `TimeGrid` and appears in no file.
6. **The face is the canonical `FaceName` encoding, plus one rule.** The data model's `validate` accepts the invalid feature handle (`FaceName{}`); P18-CMD-001 made the command layer refuse it, and the file refuses it too — `face.feature: 0` is "a target must name a feature". A valid handle naming a feature that does not exist is ordinary intent and loads ([UNRESOLVED_REFERENCES.md](UNRESOLVED_REFERENCES.md)).
7. **Identifiers are capped on write as well as on read.** `validate` asks only that an identity be valid, and the reader caps a file identifier at 2^53−1, so an identity above the cap used to be writable and then unreadable. The writer refuses it, by name, before writing anything (`AnIdentifierAboveTheFileCapIsRefused…`; found by the independent review).

## What the schema does not have, by name

`K`, `C`, `Q`, a DOF map, a constraint or free-equation map, prepared loads, prepared conditions, a node, an element, a facet, a temperature, a temperature history, a gradient, a heat flux, a reaction, a balance, a validation report, a currentness flag, a revision, a timestamp, a duration, an iteration count, a conductivity, a density, a specific heat, a material. [AUTHORITY.md](AUTHORITY.md) shows how that is enforced.
