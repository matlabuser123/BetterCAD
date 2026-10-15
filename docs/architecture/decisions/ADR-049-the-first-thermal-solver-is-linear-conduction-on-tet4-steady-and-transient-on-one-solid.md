# ADR-049 — The first thermal solver is linear conduction on Tet4, steady and transient, on one solid

```text
STATUS:    Accepted
DATE:      2026-10-16
MILESTONE: P18-ARCH-001
TOUCHES:   ADR-034 (the first structural solver, whose shape this follows),
           ADR-031 (Tet4 only), ADR-027 (thermal properties are constants with
           a law contract for later), ADR-028 (a solver holds no material data)
```

## Context

P17 delivered a linear-static structural solver and qualified it in seventeen
milestones. P18 is the second phase that computes an engineering answer from
the same authority chain -- CAD geometry, a P15 material, a P16 Tet4 mesh --
and the first whose field is a scalar rather than a vector.

The argument ADR-034 made about scope applies unchanged: a temperature that is
wrong because the formulation does not cover the case looks exactly like a
temperature that is right. The failure mode of an under-stated scope is a
plausible wrong answer, not a visible gap.

Two things are different from P17's starting position. P15 already names this
phase's consumers by name -- `ConsumerKind::ThermalSteady`,
`ConsumerKind::ThermalTransient`, `requireThermalConductivity`,
`requireTransientConductionProperties` -- and already fixes the shape a
temperature-dependent property law will take (ADR-027, `PropertyLawKind`,
`OutOfRangeBehaviour`). And the roadmap names the coupling that follows:
"temperature fields eventually couple into structural analysis for thermal
expansion and stress". Both constrain what this decision may leave open.

## Constraints

- `ADR-031` fixed Tet4 as the only element family. A linear tetrahedron has a
  constant temperature gradient per element, so heat flux is element-constant
  and converges slowly at a flux concentration, exactly as stress does. Known,
  recorded, inherited.
- P16 refuses a body with more than one solid. A composite wall of two
  materials is therefore NOT a two-body model: it is one solid, or it is not
  meshable today. RM-THERM-03's layered conduction must be built on one solid
  with one material, or wait.
- P15 stores CONSTANT properties: `k`, `rho`, `cp` are numbers, and ADR-027
  fixes that a temperature-dependent law arrives as a new value representation
  behind the same `requireX` call, not as a restructured consumer.
- `ADR-028` forbids a downstream solver holding material data of its own, and
  names P18.
- This toolchain has no ASan/UBSan.

## Options

### A — state the supported case narrowly and refuse everything else

Linear conduction, constant isotropic `k`, one solid, Tet4, one scalar
temperature DOF per node; steady state and implicit transient; the loads and
boundary conditions P15's consumers imply. Anything else is an explicit
refusal with a diagnostic.

### B — build a general transport framework first

A scalar-field interface with conduction as its first instance, ready for
radiation, phase change and a coupled fluid.

### C — support the narrow case and treat unsupported input as best-effort

Solve what can be solved, warn about the rest.

## Decision

**Option A.** The first supported thermal analysis is exactly:

```text
physics        heat conduction in a solid, Fourier's law q = -k grad(T)
material       homogeneous, isotropic, CONSTANT k; constant rho and cp for a
               transient
element        Tet4, the four-node linear tetrahedron, from P16
DOF            T -- one scalar temperature per node
geometry       one current regenerated CAD solid
mesh           one current validated P16 Tet4 volume mesh

steady         K T = Q
transient      C dT/dt + K T = Q, integrated implicitly (backward Euler) from
               a prescribed initial temperature over a fixed step and end time

loads          surface heat flux q'' on a CAD face [W/m^2]
               volumetric heat generation q''' in the body [W/m^3]
boundary       prescribed temperature on a CAD face [K]
conditions     convection (Robin) on a CAD face: -k grad(T).n = h (T - T_inf)
```

### The mathematics this commits to

```text
K    = sum over elements of  V_e  G_e^T  k  G_e          [W/K]
C    = sum over elements of  integral rho cp N^T N dV     [J/K]
Q    = assembled nodal heat input from q'', q''', and the
       convection source h T_inf A                       [W]
grad(T) constant per element; q = -k grad(T)             [K/m], [W/m^2]
```

`K` is symmetric and positive semi-definite with exactly one null mode on a
connected solid -- the uniform temperature -- which a prescribed temperature or
a convection surface removes. Whether the capacity matrix is consistent or
lumped is `P18-ELEM-001`'s decision and is NOT fixed here; what is fixed is that
it is one policy, stated, and that `C` is symmetric positive definite.

Temperatures are ABSOLUTE, in kelvin, as P15's `Temperature` is; a boundary
condition stated in degrees Celsius is a unit conversion at the boundary, not a
second temperature type.

### Unsupported, and refused rather than approximated

```text
radiation                  phase change               thermal contact
anisotropic conductivity   temperature-dependent k,    nonlinear boundary
                             rho or cp                   conditions
fluid / CFD coupling       thermo-mechanical coupling  moving heat sources
Joule heating              chemical heat generation    adaptive remeshing
multiple-solid assemblies  Tet10 / Hex8                explicit time integration
```

An unsupported request is an explicit diagnostic. Never a best-effort answer,
never a warning beside a number.

### What is deferred to a named milestone, and why it is not scope

```text
consistent vs lumped capacity               P18-ELEM-001, with the measurement
                                            that decides it
the time-varying load and BC policy         P18-TRANSIENT-001; the data model
                                            must leave room (P18-DATA-001)
the heat-flux and convection sign           P18-LOAD-001 / P18-BC-001, frozen
conventions, written once                   there and shared by balance and
                                            post-processing
the heat-balance tolerance                  P18-BALANCE-001, from measurement
the thermal mesh acceptance policy          P18-VALID-001; P16 measures, P18
                                            judges, as P17 did
```

## Rationale

Against Option B: a transport framework built before one scalar solver works is
designed from imagination. The cheap part of generality -- the element records
its type, the material law has a fixed extension shape -- is already paid by
ADR-031 and ADR-027. The expensive part would be invented.

Against Option C, and bluntly: a best-effort temperature is worse than a
refusal, because the user cannot see the difference. The project's stated
priorities are correctness, diagnostics and reproducibility ahead of features.

Why transient is IN scope when P17 left dynamics out: P15 already split the two
consumers and the transient one needs exactly two more constants from the same
material; the implicit backward-Euler step reuses the steady system with a
shifted matrix; and the brief's analytical benchmarks (a lumped body, a 1D
slab) exist in closed form. The cost is one more solver milestone, not a new
formulation.

Why thermo-mechanical coupling is OUT although it is the roadmap's next
sentence: it changes P17's formulation (a thermal strain term in the structural
load), which is a change to a qualified module and a separate scope decision.
What this ADR does for it is leave the door open structurally -- ADR-050 puts
`thermal` BELOW `structural` so the coupling can be written without a renumber.

The single-solid limit is not a preference. It follows from P16 refusing a
multi-solid body, and lifting it means changing P16 first.

## Consequences

**Easy.** Validation against closed forms: the linear-temperature patch test,
the 1D slab, uniform generation, the Robin slab and the lumped transient all
have exact answers because the formulation is the one the derivations assume.

**Hard.** Flux concentrations. Element-constant flux on Tet4 is the thermal
twin of P17's F2, and a heat-flux reference case near a corner or a small
convective patch must be a convergence study rather than a single comparison.

**Committed.** To refusing, in code, cases a commercial package would attempt:
radiation, contact, a property law, a second solid. Each is a missing feature
rather than a wrong number, and the distinction is the point.

## Verification

Scope cannot be proved by a test; it is a statement about what is NOT built.
What is verified is that the statement and the code agree:

```text
src/thermal/ holds one translation unit: the input boundary. No element, no
  matrix, no DOF, no time step, no linear algebra linked.
the single-solid limit is P16's, already tested there
the conductivity comes from P15's requireThermalConductivity, already validated
  there (missing, non-finite and non-positive k refused, the material named)
```

The reference models and the acceptance policy that test the formulation belong
to `P18-ELEM-001`, `P18-VALID-001` and `P18-REFMOD-001`.

Evidence: [docs/verification/P18-ARCH-001/](../../verification/P18-ARCH-001/README.md).
