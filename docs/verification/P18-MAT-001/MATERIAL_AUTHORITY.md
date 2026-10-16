# P18-MAT-001 — one material authority

```text
P15 canonical material                YES  -- the only one
P18-owned canonical material database NONE
thermal property authority            P15
```

## What `ThermalMaterial` is, and is not

```cpp
class ThermalMaterial {           // DERIVED, source-bound, disposable
    MaterialId id();              // reference back to P15
    std::uint64_t revision();     // the material's document revision when resolved
    ThermalAnalysisMode mode();   // which consumer this view serves
    ThermalConductivity conductivity();                   // k, always
    const std::optional<ThermalCapacity>& capacity();     // rho + cp, iff transient
};
struct ThermalCapacity { Density density; SpecificHeatCapacity specificHeatCapacity;
                         VolumetricHeatCapacity volumetric() const; };   // rho cp, derived
```

| It is | It is not |
| --- | --- |
| rebuilt on every call from P15's values | cached: there is no cache, keyed by anything |
| an id and a revision pointing back at the canonical record | a copy of the record: no name, no designation, no provenance text, no library key -- `sizeof` asserted equal to exactly those five members |
| what THIS analysis mode consumes | what the material has: a steady view omits a density the material may carry |
| built only by `resolveThermalMaterial` | default-constructible, settable or constructible from a `MaterialDefinition` |
| disposable: holding one across an edit gives a view with an old revision, which `ThermalResultSource` will call stale | a persistence or undo payload |

## Searched, and absent

```text
in include/bettercad/thermal/ and src/thermal/

a map, set or array of materials      0      no unordered_map, no library, no preset
a default k, rho or cp                0      the only literals in the module are
                                             diagnostic strings; 7800, 500, 45, 1.0,
                                             300, 293 appear only in tests and in
                                             comments that forbid them
value_or                              2      both in HeatBalance (P18-DATA-001): a
                                             steady balance has no stored-energy term
                                             and the arithmetic treats "no term" as
                                             zero contribution. Not a material value;
                                             not a fallback for a missing property
std::abs / fabs on a property         0      the two textual hits are comments saying
                                             nothing does it
clamp, epsilon, nearest               0
a thermalDensity                      0      rho is read from MechanicalProperties,
                                             the material's one density, as P15 said
```

## Why the analysis definition cannot become a material

`ThermalAnalysisDefinition` has no field for `k`, `rho` or `cp`, and
P18-DATA-001's size mirror (`ThermalData_TheDefinitionHoldsOnlyIntent`)
breaks the build if one is added. The analysis names a `MeshControlId`; the
material is reached through the document's assignment at resolution time, as
P15 and P17 do. P18-PERSIST-001 will therefore save no property under the
thermal analysis; the material's own schema is P15's.

## Why there is no cache

Resolving is five lookups: find the body, read the assignment, find the
material, read two or three `MaterialProperty` values, read a revision. That
is nothing beside a solve, and a cache would have to be keyed by exactly the
`(MaterialId, revision)` pair that `ThermalResultSource` already tracks --
a second copy of the same currency. Recomputing is both simpler and
impossible to get stale. If profiling ever shows otherwise, the key is the
revision pair and never the id alone.

## Commands and persistence, audited

```text
P18 commands editing materials      none exist and none are planned: material
                                    editing is P15's (setMaterialMechanical,
                                    setMaterialThermal, ...), and a later P18
                                    command that needed one calls P15's
P18 result invalidation             dynamic, from the material revision in the
                                    source; no hook, no flag, no notification
saved document                      P15's material schema holds k, rho, cp once;
                                    a thermal analysis cannot yet be saved at all
                                    (P18-DATA-001 pinned the refusal) and when it
                                    can, it carries no property
```
