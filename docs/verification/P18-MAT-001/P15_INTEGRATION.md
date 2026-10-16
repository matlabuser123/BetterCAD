# P18-MAT-001 — what P15 already owns

```text
SUBJECT:  every property, requirement and validation this milestone's
          checklist names, and who actually owns it
METHOD:   read the qualified code before writing any. The finding is that P15
          owns all of it, and owns it for THIS consumer by name -- and that
          P17-MAT-001 had already measured the same thing for the structural
          consumer, so the shape was known before the first line.
RESULT:   NO P15 SCHEMA CHANGE. Nothing in core/materials, features/material
          or the document format moved.
```

## The audit table

| Property | P15 canonical field / type | Canonical unit | Optional? | Current validation (P15) | P18 steady required? | P18 transient required? |
| --- | --- | --- | --- | --- | --- | --- |
| `k` thermal conductivity | `materials::ThermalProperties::thermalConductivity`, `MaterialProperty<ThermalConductivity>` | W/(m K) | Yes: `Unknown` by default | at entry (`validate(ThermalProperties)`: finite, > 0, not Derived); at consumption (`requireThermalConductivity`: present, finite, > 0, names the material) | **yes** | **yes** |
| `rho` density | `materials::MechanicalProperties::density`, `MaterialProperty<Density>` -- the material's ONE density, in the mechanical half | kg/m³ | Yes | at entry (`validate(MechanicalProperties)`); at consumption (`requireDensity`, `requireTransientConductionProperties`) | **no** | **yes** |
| `cp` specific heat capacity | `materials::ThermalProperties::specificHeatCapacity`, `MaterialProperty<SpecificHeatCapacity>` | J/(kg K) | Yes | at entry (finite, > 0); at consumption (`requireTransientConductionProperties`) | **no** | **yes** |

## The rest of the P15 model, as found

| Concern | P15 mechanism | P18 use |
| --- | --- | --- |
| Material identity | `MaterialId` (`Id<MaterialIdTag>`, a document object; widens to `ObjectId`); allocated by the document's one `IdAllocator`, never reused | referenced, never copied |
| Material assignment | one optional `MaterialId` per DOCUMENT (ADR-026): no per-body assignment, no inheritance, no default. `materialAssignment()` distinguishes `Unassigned`, `Resolved`, `Unresolved` (deleted) and `Invalid` | consumed through `requireEffectiveMaterial`, which turns the last three into distinct diagnostics |
| Body → material | the body is not a selector today; it exists or it does not | the resolver takes the body, establishes it exists, and reads the document's assignment (P17-MAT-001's shape, same reason) |
| Property representation | `MaterialProperty<T>`: `Unknown`, `Known` or `Derived`; `value()` is an `optional`, there is no `valueOr()` by design | read through P15's `require*` functions only |
| Missing vs invalid | `Unknown` is empty, never zero; an invalid value is REFUSED at entry, so it cannot be stored. `isAvailable` counts an out-of-range value as absent | both halves inherited; the resolver never sees an invalid stored value |
| Revision / generation | `DocumentObject::revision()` on the `Material`: starts at 1, +1 on every effective `setDefinition`; a no-op edit does not move it | the provenance half of `ThermalMaterial` and of `ThermalResultSource` |
| Persistence | P15's own material schema in the document file (P15-PERSIST); `k`, `rho`, `cp` are already saved there | nothing thermal-specific is saved; P18-PERSIST-001 will save the analysis's `MaterialId`-free definition and NO property |
| Editing APIs | `setMaterialMechanical`, `setMaterialThermal`, `removeMaterialProperty`, `assignMaterial`, `removeMaterialAssignment`, `removeMaterial`, `cloneMaterial`; every edit validates and fails atomically | exercised by the tests; none wrapped, none duplicated |
| Validation APIs | `materials::validate(...)`, `features::require*`, `materials::requiredProperties(ConsumerKind)`, `features::materialCompleteness`, `effectiveMaterialCompleteness` | all consumed; none reimplemented |
| Requirement table | `requiredProperties(ThermalSteady) = { k }`; `requiredProperties(ThermalTransient) = { Density } + { k, cp }` -- "THE one definition; nothing else may hold a second copy" | **read**, never restated |
| Temperature dependence | not stored. `PropertyLawKind::Constant` is the only implemented law; a `referenceTemperature` is a RECORD, not a function (ADR-027) | nothing to sample, nothing to reject; a constant is a constant |
| Anisotropy | not representable: one scalar `ThermalConductivity` | nothing to reduce |

## What this means for the checklist

```text
Resolve k / rho / cp from P15             consumed through P15's boundary functions
Define steady / transient requirements    READ from requiredProperties(consumerFor(mode))
Reject missing k / rho / cp               P15's require* functions refuse and name them
Reject non-finite, require > 0            P15 refuses AT ENTRY; unrepresentable in a document
Preserve provenance                       MaterialId + Material::revision(), as P17
Material edit stales result, not mesh     the material revision is in ThermalResultSource
                                          (P18-DATA-001); the mesh stamp is independent
No P18 material database                  nothing stores a property; see MATERIAL_AUTHORITY.md
```

## What P18 therefore adds

Three things, and they are the three P15 cannot know:

```text
consumerFor(ThermalAnalysisMode)    WHICH consumer this analysis is -- intent, a
                                    field of ThermalAnalysisDefinition
resolveThermalMaterial              one solver-ready view per mode, carrying the
                                    material's id and revision as references back
                                    to P15, k always, and a rho/cp pair exactly
                                    when the mode needs one
currentResultSource (P18-DATA-001)  now resolves the material FOR THE ANALYSIS'S
                                    MODE, so a transient analysis on a k-only
                                    material is InputsUnavailable rather than Ready
```

The P18-ARCH-001 input boundary (`requireThermalModel`) is unchanged: it
accepts a k-only material, as ADR-051 decided, and the per-mode question is
answered one step later by this milestone, which is where ADR-051 said it
would be.
