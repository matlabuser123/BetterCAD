# P18-PERSIST-001 — round trip, and save → load → re-solve

```text
create -> solve -> save -> DESTROY every in-memory object -> load -> regenerate the mesh through P16 -> assemble -> solve -> recover -> balance -> validate
nothing is carried across but the file
```

## Canonical intent, field by field

`ThermalPersistence_TheWholeSteadyCanonicalIntentRoundTrips` and `…TransientCanonicalIntentRoundTrips` build the golden fixtures and compare **three ways**: a fingerprint that names every field at seventeen significant digits and reads no revision, mesh or result; the field-complete defaulted `operator==` of the whole definition (so a member added later without being serialized fails here even if the fingerprint forgot it); and a non-vacuity check that the fingerprint really contains every load, condition and setting.

**The golden values are deliberately not round**, none of them is a default, and no two are alike, so a swapped field, a rescaled unit or a default standing in for a dropped field shows:

| Field | Value | A mistake it would expose |
| --- | --- | --- |
| surface flux (load 4) | −1234.5 W/m² | a lost sign (`abs`), a kW rescale |
| generation (load 2) | 67890.25 W/m³ | a transposed field |
| zero flux (load 7) | 0.0 W/m² | zero treated as absent |
| held temperature (condition 3) | 345.125 K | a Celsius conversion (71.975) |
| convection h (condition 1) | 27.75 W/(m² K) | a kW rescale, `abs` |
| ambient | 291.625 K | h and T∞ swapped |
| initial temperature | 312.875 K | a Celsius conversion, a default |
| time step / end time | 0.125 s / 2.375 s | milliseconds, minutes (19 whole steps) |
| solver | 1.23456789e−8 / 2.5e−11 | a low-precision formatter; a default (1e−9, 1e−12) |
| order | loads 4, 2, 7; conditions 3, 1 | a sort by id |

Beyond the golden:

| Test | What it shows |
| --- | --- |
| `EveryEnumeratorRoundTrips` | both modes, both output policies, both load kinds and both condition kinds, each put on an analysis in turn (a key table cannot check its own entries) |
| `SignedLoadsKeepTheirSignAndTheirZero` | +, −, 0 for both load kinds — no `abs()`, no clamp |
| `ExtremeAndAwkwardFiniteValuesSurviveBitForBit` | `0.1 + 0.2`, the smallest subnormal, the largest finite double, **−0.0**, 0 K, 1e−300 — compared as `bit_cast<uint64_t>` (the defaulted `==` cannot see the sign of zero) |
| `LoadAndConditionOrderIsTheUsersAndIsPreserved` | ids 4, 2, 7 stay 4, 2, 7 |
| `AnEmptyAnalysisAndManyRecordsRoundTrip` | no records (and no empty arrays written); 3000 loads and 3000 conditions |
| `TwoAnalysesRoundTripWithoutLeaking` | the **same** load and condition ids in two analyses are two different loads; each keeps its own object id and name |
| `AThermalAndAStructuralAnalysisShareOneFileWithoutInterfering` | a P17 analysis beside it round-trips, and the thermal object's bytes are identical with or without it (the whole object, not a prefix) |
| `ALoadedDocumentKeepsItsIdentitiesAndCommandsStillWork` | ids from a file are as good as ids from this session: an add that reuses one is refused, an unused one is accepted, and undo/redo are exact against a loaded analysis |

## Save → load → re-solve (the engineering test)

`ASteadyDestroyAndLoadRoundTripReSolvesEquivalently`, `ATransientDestroyAndLoadRoundTripReSolvesEquivalently`, `ATransientWithAShortenedLastStepReSolvesEquivalently`. The original study is built, solved, **reduced to plain numbers** (so no object of it survives), saved, and destroyed at the end of its scope. The file is loaded into a fresh document; a new `Regenerator` and `Mesher` (a regenerator belongs to one document and refuses another) regenerate the mesh by the normal P16 path; the normal pipeline solves. Canonical intent is compared **first**, before anything is regenerated.

| | Steady | Transient | Transient, shortened last step |
| --- | --- | --- | --- |
| nodes / elements | 9 / 24 | (the same block) | (the same block) |
| stored states | 1 | 20 (19 steps + the initial state) | 5 (0.3, 0.3, 0.3, 0.1 + initial) |
| times | one, no time | 0 … 2.375 s exactly | 0 … 1.0 s exactly — from the grid, never `n·dt` |
| nodal temperature, every state | worst relative difference **0.000e+00** | **0.000e+00** | **0.000e+00** |
| element gradient and heat-flux fields | bit-identical in 1 of 1 | bit-identical in 20 of 20 | bit-identical in 5 of 5 |
| heat-balance terms (applied, generated, convection, reaction, stored rate) | within 1e−12 | within 1e−12 | within 1e−12 |
| validation verdict and finding count | PASS, equal | PASS, equal | equal |

The comparison **requires** 1e−12 and *reports* the bit-level result. Equal bits are what a deterministic solver on a deterministic mesh gives in one build; across presets the requirement (not the bit equality) is what is guaranteed, which is why the tolerance is the contract.

### Loading restores intent and nothing else

`ALoadedAnalysisHasIntentAndNoDerivedState`: after loading a document that was **solved before the save** —

```text
canonical analysis restored:                  YES
a mesh generated by the load:                 NO   (a fresh Mesher holds nothing)
requireThermalModel on the loaded document:   REFUSED (no mesh)
validation of the loaded analysis:            FAIL (nothing derived to validate)
K, C, Q loaded as authority:                  NO
temperature loaded as current:                NO
POST, BALANCE, VALID loaded as current:       NO
a result held from BEFORE the save:           NOT current for the loaded document
```

and `AResultOfOneDocumentDoesNotAttachToACopyThatDiffers`: two documents loaded from one file hold the same identifiers. The copy is edited — through the command layer — until its analysis **revision equals the original's** (the aliasing case for a revision-only comparison) while its content differs. The original's result is still stale for it, for exactly two reasons: its **mesh** (a result is bound to the stamp of the mesher that made it) and its **loads** (the derived chain compares the canonical intent *by value*, the first of the two currentness regimes recorded by P18-CMD-001). An *unedited* copy is stale for the mesh alone. No result attaches to a document by identifier.

### A face resolves to the same geometry

`AReloadedFaceLoadResolvesToTheSameGeometry`: the reference is canonical and the facets are derived. After a save and a load the same `FaceName` selects the same part of the boundary: the heat balance's applied term is **−1234.5 W/m² × 6.0×10⁻⁴ m² = −0.7407 W** — a hand value (the face is 30 mm × 20 mm) — before and after, to 1e−9.

### The hand-written file solves

`TheReferenceFileRegeneratesMeshesAndSolves`: `examples/models/thermal_plate.bcad` (100 × 50 × 20 mm plate with a 20 mm hole, S355 with k = 45 W/(m K), c = 460 J/(kg K)) loads, meshes, prepares, assembles, solves (20 stored states ending at exactly 2.375 s), recovers and balances from the file alone, and its validation is not `Fail`.
