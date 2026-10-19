# P18-BC-001 — the boundary-condition model

```text
SUBJECT:  what is canonical, what is derived, what already existed, what this
          milestone decided, and what it refused to decide
STATUS:   written before the first line of production code and kept in step with it
```

## What already existed (the audit)

| Concern | Existing mechanism | Reusable? | Decision | Reason |
| --- | --- | --- | --- | --- |
| BC identity | `ThermalBoundaryConditionId` (P18-DATA-001) | yes | reuse | persisted, distinct from `RestraintId` and `ThermalLoadId`; three compile-fail cases keep them apart |
| BC variants | `ThermalBoundaryCondition` = `PrescribedTemperature {FaceName, Temperature}` or `ConvectionBoundary {FaceName, HeatTransferCoefficient, Temperature}` (P18-DATA-001) | yes | reuse unchanged | already canonical, already SI, already validated |
| Temperature policy | `isAbsoluteTemperature`: finite and not below 0 K | yes | reuse | the one rule; a BC-layer rule of its own is forbidden |
| Constraint map | `ThermalConstraintMap`, `buildThermalConstraintMap` (P18-DOF-001) | yes | feed and annotate | sorts by unknown, collapses exact duplicates, refuses a contradiction; this milestone adds attribution and the structured conflict report |
| DOF numbering | `ThermalDofMap` (P18-DOF-001) | yes | consume | `NodeId` is never an index |
| Face target resolution | `thermal::resolveFaceTarget` / `faceTargetProblem` (P18-LOAD-001) | yes | consume | thermal's one copy of the three-way triage, written to be shared with this milestone |
| Facet area | `meshing::triangleArea` (P16) | yes | consume | unsigned, metres |
| Currentness pattern | `PreparedThermalLoads` source (P18-LOAD-001) | pattern | mirror | a value, not a hash; body + mesh stamp + node count + geometry + the canonical list by value |
| Structural precedent | P17-BC-001 | pattern only | not reused | its restraint holds a displacement at ZERO, so it records only WHICH unknowns; a temperature is arbitrary |
| Convection formulas | none | no | derive (WEAK_FORM.md) | new |
| Convection element | `Tet4Conduction` is a 4x4 over a tetrahedron | no | new 3x3 triangle kernel | a boundary term lives on a facet |

## Canonical and derived

| Item | Class | Where it lives |
| --- | --- | --- |
| `ThermalBoundaryConditionId` | CANONICAL | `ThermalData.hpp` |
| BC type (prescribed temperature or convection) | CANONICAL | `ThermalBoundaryCondition` variant |
| `GeometryReference` = `FaceName` | CANONICAL | in the condition |
| `T_bar` [K] | CANONICAL | `PrescribedTemperature::value` |
| `h` [W/(m² K)] | CANONICAL | `ConvectionBoundary::coefficient` |
| `T_inf` [K] | CANONICAL | `ConvectionBoundary::ambient` |
| resolved boundary facets | DERIVED | `PreparedTemperatureBoundary::facetCount`, `ConvectionFacet::facet` |
| resolved `NodeId`s | DERIVED, transient | passed through `uniqueFacetDofs`, never stored |
| `ThermalDofIndex`es | DERIVED | `PreparedTemperatureBoundary::dofs`, `ConvectionFacet::dofs` |
| temperature constraint map | DERIVED | `PreparedThermalBoundary::constraints()` |
| constraint attribution | DERIVED | `constraintSources(index)` |
| Robin local matrices `Kconv` | DERIVED | `ConvectionFacet::term` |
| Robin local source vectors `Qconv` | DERIVED | `ConvectionFacet::term` |
| resolved area | DERIVED | summed from the facets on every call; never stored |
| prepared BC provenance | DERIVED | `PreparedThermalBoundarySource` |

A canonical condition holds no facet, node, element or unknown. The mirror-struct
`sizeof` tests fix the members, and the compile-fail group names each absent one.

## The two kinds combine differently

```text
PRESCRIBED TEMPERATURE   a MATHEMATICAL constraint on an unknown
                         same unknown, same value     one constraint (deduplicated)
                         same unknown, different      REFUSED -- not first, not last, not the mean
CONVECTION               a PHYSICAL exchange with an ambient
                         two conditions on one face   two exchanges: they ADD, even if identical
```

One generic "deduplicate the conditions" function would lose that distinction, so
there is none: constraints go through `resolveTemperatureConstraints`, convection
terms are kept per condition in the order given.

## Policies decided here (the brief asked for each to be explicit)

| Question | Decision | Test |
| --- | --- | --- |
| prescribed temperature and convection on the same face | **both kept.** The temperature is imposed exactly by the later solve; the convection is still part of the heat exchange and the reaction balance | `DirichletAndRobinOverlapPolicy`, `PreparedBoundaryHasNoPenaltyAssemblyOrSolve` |
| overlapping convections | **additive, never deduplicated**, including two identical ones | `ConvectionSuperposition`, `ConvectionSuperpositionOnAMappedFace` |
| identical overlapping prescriptions | **deduplicated** to one constraint, both conditions recorded as having asked | `DuplicateIdentical...`, `IdenticalOverlapAlongAnEdgeDeduplicates` |
| a model with no prescribed temperature | **valid at this layer.** A convection face with `h > 0` removes the null mode by itself; solvability is the solver's question | `NoDirichletBoundarySetIsValid` |
| every unknown prescribed (`Nfree = 0`) | **valid** | `SeamRefusalsOfTheConstraintMap` |
| conflict equality | **exact canonical number equality.** 300 K and the next double above it conflict; no solver tolerance merges two commands | `ConflictUsesExactCanonicalEquality` |
| `h = 0` | **the CANONICAL record refuses it; the kernel and the seam are total over `h >= 0` and give all-zero terms** | `ZeroConvectionCoefficientPolicy` |
| a malformed ambient beside `h = 0` | **refused** whatever the coefficient is | same |
| ambient of 0 K | **valid**; `Qconv = 0`, `Kconv` remains | `BelowZeroKelvinAmbientRejectedAndZeroKelvinAccepted` |

### The `h = 0` decision, stated plainly

The brief recommended allowing `h = 0` and said not to require `h > 0` "without
architectural reason". There is one on the record. P18-DATA-001 froze the
canonical rule as finite and **strictly positive** ("a zero coefficient exchanges
nothing and is an adiabatic face wearing a convection record"), its adversarial
review carries a mutation (`h = 0` accepted) that its tests kill, and changing it
would reopen a qualified milestone's contract. So:

- the canonical record and `prepareThermalBoundary` keep refusing `h = 0`;
- the kernel (`computeTri3Convection`) and the integration seam
  (`integrateConvection`) accept it and return exactly zero, so the mathematics
  the brief asks for at `h = 0` is implemented and tested;
- the one-line change to allow it canonically (`<= 0.0` to `< 0.0` in
  `validate(ThermalBoundaryCondition)`, plus the one test and the sentence in
  `ThermalData.hpp`) is recorded as an owner decision.

## What the model refuses to be

No penalty method, no epsilon anchor, no global assembly, no reduced system, no
solve, no radiation, no time-varying coefficient or ambient, no contact. Each is
absent from the types (compile-fail cases) and from the source (SCOPE.md).
