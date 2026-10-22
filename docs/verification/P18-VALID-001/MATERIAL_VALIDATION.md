# P18-VALID-001 — thermal material validation

```text
RULE:     TV_MATERIAL (V05)
SUBJECT:  the material must supply what THIS analysis MODE needs, resolved through P15 and P18-MAT-001, with no material data of its own
```

## The table

| Property | Steady | Transient | Finite | Sign | Refused by | Missing property reported as |
| --- | --- | --- | --- | --- | --- | --- |
| `k` thermal conductivity | **required** | **required** | yes | `> 0` | P15 at entry (creation and every edit) | `material_conductivity_unusable` |
| `rho` density | not required | **required** | yes | `> 0` | P15 at entry | `material_incomplete_for_mode` |
| `cp` specific heat | not required | **required** | yes | `> 0` | P15 at entry | `material_incomplete_for_mode` |
| no material assigned / body gone | — | — | — | — | — | `material_missing` |

The requirement table is READ, not hard-coded: `thermalMaterialProblem(document, body, mode)` asks P15's `requiredProperties(consumerFor(mode))`, so P15's table and this
rule cannot disagree. The validator holds no material library, no typical conductivity and no fallback (ADR-028).

## Mode, not blanket

* **Steady needs `k` only.** A material with the density and the specific heat REMOVED serves a steady analysis end to end: `SteadyRequiresConductivityOnly` runs the
  whole chain on a `k`-only steel and the report is `PASS`, with `TV_MATERIAL` passing. Requiring `rho`/`cp` of a steady analysis would reject a valid model for a property
  it never reads.
* **Transient needs `k`, `rho` and `cp`.** `TransientRequiresConductivityDensitySpecificHeat` takes the same `k`-only material and a transient analysis: `TV_MATERIAL`
  FAILS with `material_incomplete_for_mode` (and not `material_conductivity_unusable`, because `k` IS usable). Density alone missing and specific heat alone missing are
  each refused; all three present is accepted. There is NO zero-capacity fallback: a transient analysis without a capacity does not run on an invented one.
* **The distinction survives into the code.** `material_conductivity_unusable` means the conductivity that every mode needs is the problem; `material_incomplete_for_mode`
  means the mode needs more than the material has. The classification asks `thermalMaterialProblem(…, Steady)` to tell them apart.

## Independence from the mesh

The material is judged as soon as the control names a body, so a missing mesh does not hide it and it does not hide a stale mesh (`RejectsMissingMesh` shows
`TV_MATERIAL` passing beside a failed `TV_MESH_CURRENT`; `RejectsMissingMaterial` shows the mesh rules PASSING beside a failed `TV_MATERIAL`). A material edit stales the
assembled system, the solution, the recovered fields and the balance and does NOT stale the mesh and does NOT ask for a remesh: after a conductivity, density or
specific-heat edit `mesher.currency` is still `Current` (`EveryEditStalesExactlyWhatDependsOnIt`, `EveryTransientEditStalesTheHistory`).

## Non-finite and non-positive values never reach the validator

The brief asks that a non-finite or non-positive required property is rejected. P15 makes such a value UNREPRESENTABLE: creation and every edit refuse it, so a
document, and therefore the validator, never holds one. `NonFiniteAndNonPositiveThermalPropertiesNeverReachTheDocument` shows the refusal for `k`, `cp` and `rho` at
`0`, `-3`, NaN and infinity — twelve refused edits — and shows the document revision unchanged by all of them. The validator does not duplicate that check; it
relies on the owner and says so. A conductivity that P15 would allow to be removed is refused by the rule (`RejectsAMaterialWithoutAConductivity`); where P15 does not
allow the removal the test records that there is nothing for the validator to see.

## Source currentness

The material's identity AND revision are in `ThermalResultSource` and in the assembled system's source (P18-ASSEMBLY-001), so old `T` cannot be paired with a new `k`:
the matrix in SOURCE_CURRENTNESS.md shows a conductivity edit marking `system`, `solution`, `post_result` and `balance_result` stale.
