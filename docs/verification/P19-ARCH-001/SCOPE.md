# P19-ARCH-001 - scope

P19-ARCH-001 is the first milestone of P19 - CFD Integration, authorized by the owner's
"start P19-ARCH-001" on 2026-10-27, with the phase plan recorded in `TODO.md`. It is an
**architecture** milestone: it defines the ownership model before any CFD physics exists.

## In scope (and where each checklist item is answered)

| TODO item | answered in |
|---|---|
| Define P19 supported CFD scope | ADR-058 "Supported" / "Deferred"; `SCOPE.md` below |
| Define canonical CFD intent / derived CFD state | ADR-058 "Canonical and derived"; `AUTHORITY_MODEL.md` |
| Define CAD authority | ADR-058 "The geometry"; `AUTHORITY_MODEL.md` |
| Define fluid/material authority | ADR-060 "The fluid" (v1: the document's single material assignment); ADR-028 (extended, not replaced) |
| Define mesh authority | ADR-058 "The mesh"; ADR-059 |
| Define CFD result authority | ADR-058 (derived, never persisted, never an authority) |
| Define source/currentness model | ADR-058 "Currentness"; ADR-060 (the boundary) |
| Define geometry-reference policy | ADR-058 constraints and "Canonical"; `GeometryReference`, never a cell, face or patch index |
| Define field-location policy | ADR-058 "Field locations and indexing" |
| Define unit system | ADR-058 "Units" |
| Define steady/transient scope | ADR-058 "Supported" |
| Define incompressible/compressible scope | ADR-058 "Supported" / "Deferred" |
| Define laminar/turbulence scope | ADR-058 "Turbulence" |
| Define thermal-coupling scope | ADR-058 "Thermal coupling"; ADR-059 (the layer leaves both one-way couplings open) |
| Define unsupported-physics policy | ADR-058 "Deferred, and REFUSED rather than approximated" |
| Define dependency layers / ADRs | ADR-059; `LAYERING.md`; `DEPENDENCY_AUDIT.md` |
| Define failure policy | ADR-058 "Failure" |
| Define determinism policy | ADR-058 "Determinism" |
| Define evidence structure | ADR-058 "Evidence"; this directory |

## What was built, so the decisions are enforced rather than described

* a `cfd` module at layer 42 with **one** translation unit: `requireCfdModel` / `CfdModel` (possession is the
  proof), seven reachable `InputProblem`s, density resolved through P15;
* the layer entry in the architecture checker, five layering fixtures and three link-graph fixtures, each holding
  one violation and asserting the message naming it, and the positive directions in the valid tree;
* a link-graph rule: `cfd` links neither the thermal nor the structural target, and names nothing outside core,
  features, meshing, linalg and eigen (an allow-list, with a fixture);
* fourteen boundary cases, zero-match guard entries for them and for the fixtures.

## Out of scope - and not stubbed

No cell, face, owner/neighbour topology, field, `CellIndex`, boundary condition, fluid-property type, viscosity,
matrix, operator, pressure-velocity coupling, solve, result, command, file schema or CLI verb. Each is a later
milestone of the plan, and the boundary header names which. In particular **no P15 change**: the dynamic viscosity
property and the CFD consumer kind are P19-FLUID-001's, and the boundary says it proves a density and nothing about
a viscosity.

## The v1 scope in one block

```text
3D, single phase, incompressible, Newtonian; constant rho, constant mu
steady laminar (SIMPLE family) and transient laminar (backward Euler)
cell-centred collocated finite volume; a cell is a P16 tetrahedron; first-order upwind first
velocity inlet, static-pressure inlet and outlet, no-slip and moving wall, slip/symmetry; uniform initial fields
gravity as a vector; a reference pressure only when no boundary fixes the pressure
isothermal: no energy equation, no coupling
```

Deferred and refused: compressible, multiphase, species, combustion, radiation, conjugate heat transfer, moving
mesh, rotating machinery, non-Newtonian, LES/DES/DNS, porous media, free surface, cavitation, temperature-dependent
properties. Turbulence: architecture first (P19-TURB-ARCH-001), a model only if the owner puts one in scope.
