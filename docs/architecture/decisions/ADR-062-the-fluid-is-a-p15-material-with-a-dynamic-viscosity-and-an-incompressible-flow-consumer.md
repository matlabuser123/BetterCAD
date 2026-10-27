# ADR-062 — The fluid is a P15 material with a dynamic viscosity, read through an incompressible-flow consumer

```text
STATUS:    Accepted
DATE:      2026-10-27
MILESTONE: P19-FLUID-001
TOUCHES:   ADR-027 (dynamic viscosity named canonical for P19), ADR-028 (no
           second material authority), ADR-058 (constant rho and mu, nu derived),
           ADR-060 (the fluid is the document's assignment), ADR-061 (the
           definition does not name the fluid)
```

## Context

ADR-058 made the v1 fluid incompressible and Newtonian with a constant density and
a constant dynamic viscosity. ADR-060 made it the document's P15 material and left
the viscosity to this milestone, because no P15 material carried one. ADR-027 had
already listed `dynamic viscosity mu (P19)` among the canonical properties, and P15's
`ConsumerKind` documentation recorded that CFD was left out until its physics was
decided. A compile-fail case (`NO_CFD_CONSUMER`) kept it out.

Five questions had to be answered. Each answer either keeps one material authority
or creates a second.

## Decisions

### 1. The viscosity is a P15 property, in the existing non-mechanical group

`ThermalProperties` gains `MaterialProperty<DynamicViscosity> dynamicViscosity`, and
`ThermalPropertyKind` gains `DynamicViscosity`, appended so every existing kind keeps
its reporting position. The group is P15's "physical and thermal" properties, and it
already holds a non-thermal one (the electrical resistivity, ADR-029). A viscosity
is a transport property beside the conductivity.

**A third `FluidProperties` group was rejected.** P15 has a mechanical/thermal seam
in about two dozen places: the requirement rows, completeness issues and reports,
the availability overloads, issue sorting, the provenance maps and overloads, the
entry validation, the per-kind feature functions, the file sections and allowed
keys, the CLI vocabulary and reports. A third group would need a third branch at
every one, and each branch is a place for the new property to diverge from the rules
every other property obeys. In the existing group, the viscosity inherits all of
them with one case per switch:

- the validation (finite, strictly positive, not Derived, reference temperature checked);
- the Known/Unknown states;
- per-property provenance, and its clearing on edit;
- removal;
- the optional file key (absent means Unknown, no format version moves);
- the CLI property code.

The cost is a name. A viscosity listed under "thermal" in the file and in a CLI
report reads oddly, and the header says so.

**Only the dynamic viscosity is stored.** The kinematic viscosity `nu = mu / rho` is
derived wherever it is needed and has no field, no key and no CLI code. Storing it
would be a second spelling of two values that could disagree (ADR-027's rule for G
and K, applied again).

### 2. One consumer, named for the physics: `IncompressibleFlow`

`ConsumerKind::IncompressibleFlow` requires `{ Density }` and `{ DynamicViscosity }`,
and **nothing thermal**: the v1 fluid is isothermal, and requiring a conductivity
would block a solvable flow (P15's own "over-requiring is as wrong as
under-requiring"). The density is the material's one density, in the mechanical
half, as transient conduction reads it. The requirement row is in P15's one table.
`features::requireIncompressibleFlowProperties` is the runtime function, and P15's
table-versus-function cross-check now covers it. It is named for the analysis, as
every consumer is, not "Cfd": a later turbulent or compressible consumer will need
different properties and will be its own row.

`NO_CFD_CONSUMER` is **retired by renaming** it to `NO_TURBULENT_FLOW_CONSUMER`.
That keeps its purpose, "a consumer exists only for physics that has been decided":
turbulence is not decided, and P19-TURB-ARCH-001 comes first.

### 3. CFD resolves the fluid through P15 and holds a reference back, not a record

`cfd::resolveCfdFluid(document)` returns a `CfdFluid`: the material's `MaterialId`,
its revision at resolution, and rho and mu by value, with nu computed on demand. It
has a private constructor and one friend, so possession proves the resolution.

- **Success and the diagnostic** come from `requireIncompressibleFlowProperties`,
  unchanged, naming the material and every gap.
- **The category of a failure** (no material, density, viscosity) comes from P15's
  table and P15's `isAvailable`.
- **Nothing CFD-side is restated**: no range check, no default, no "air", no "water",
  no fluid database and no "is a fluid" flag. A fluid is a material that carries a
  viscosity. P19-DATA-001 had left one such copy, `validate(CfdFluidProperties)`,
  which restated P15's "finite and positive" for rho and mu and had no production
  caller. The independent review found it, and this milestone **retired** it.
- **The completeness report is P15's, unreinterpreted.** That includes P15's
  material-level findings, which P15 reports for every consumer: an inconsistent
  material (a yield strength above its ultimate strength) is `Invalid` in
  `cfdFluidCompleteness`, while `resolveCfdFluid`, which needs only rho and mu,
  accepts it. P18's thermal resolution behaves the same. It is P15's policy and is
  recorded here, not re-decided for one consumer.

The input boundary holds a `CfdFluid`. A missing viscosity is a new named input
problem, `FluidViscosityUnusable`, appended after `FluidDensityUnusable`, and the
diagnostic names both gaps when both are missing.

### 4. A fluid edit stales the result through the material revision, and nothing else

The result's source already records the fluid's id and revision (ADR-061). A
viscosity or density edit moves the revision, so the result goes stale with the
single reason `fluid`. The geometry revision and the mesh do not move, so there is
no remesh. Re-entering an equal value is not an edit, so nothing moves. **Recorded
as conservative:** the source tracks the material's revision, not a per-property
one, so an edit to a property a flow does not read (a conductivity) also stales a
flow result. That is the stale-when-unsure side, and it is what P18-MAT-001 recorded
for conduction. A per-property revision would be a second revision scheme P15 does
not have.

### 5. Old files and old builds

The new key is optional. Every document written before this milestone loads
unchanged with the viscosity Unknown, and no format version moves; that is P15's
pinned policy for optional properties. An older build refuses a file that carries
the key, naming it as an unknown thermal property. That is the existing,
deliberate behaviour for an unknown key, and it is the honest one: an older build
cannot preserve a value it does not know.

## Consequences

- Every switch over `ThermalPropertyKind` has one more case: the completeness
  availability, the provenance clearing, removal, presence, the file reader and
  writer, and the CLI format, unit and setter. Tests that pinned the old counts
  were moved deliberately: 5 to 6 thermal kinds, 7 to 8 consumers, 14 to 15
  property codes.
- P19-ARCH-001's tripwire (`CfdInput_AcceptsADensityWithNoViscosity...`, built to
  fail the day a CFD consumer appeared) was replaced by
  `CfdInput_RefusesADensityWithNoViscosity`.
- A material can now be shown to be "ready for incompressible flow" from the CLI
  (`material-completeness --consumer incompressible_flow`).
- Per-body materials remain a prerequisite of any fluid-and-solid document
  (ADR-060).

## Verification

`tests/features/FluidPropertyTests.cpp` (`[features][fluid]`),
`tests/cfd/CfdFluidTests.cpp` (`[cfd][fluid]`), the updated P15 consumer, file and CLI
tests, `cli.material.completeness.incompressible-flow`, and the renamed compile-fail
case; evidence in `docs/verification/P19-FLUID-001/`.
