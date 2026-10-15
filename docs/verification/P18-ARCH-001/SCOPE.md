# P18-ARCH-001 — the thermal solver's scope, stated so it can be refused

```text
DECISION:  ADR-049
RULE:      an unsupported request is an explicit diagnostic, never a
           best-effort answer and never a warning beside a number
```

## Supported, initially

```text
physics        heat conduction in a solid; Fourier's law  q = -k grad(T)
material       homogeneous, isotropic, CONSTANT k [W/(m K)]
               constant rho [kg/m^3] and cp [J/(kg K)] for a transient
element        Tet4, the four-node linear tetrahedron, from P16
DOF            T, one scalar absolute temperature per node [K]
geometry       one current regenerated CAD solid
mesh           one current validated P16 Tet4 volume mesh

steady         K T = Q
transient      C dT/dt + K T = Q, backward Euler, prescribed initial
               temperature, fixed step and end time, output times

loads          surface heat flux q'' on a CAD face            [W/m^2]
               volumetric heat generation q''' in the body     [W/m^3]
boundary       prescribed temperature on a CAD face           [K]
conditions     convection (Robin) on a CAD face, h and T_inf  [W/(m^2 K)], [K]

recovery       nodal T; element-constant grad(T) [K/m] and q [W/m^2]
balance        steady heat balance; transient stored-energy balance
```

## The mathematics this commits to

```text
Ke = V_e G_e^T k G_e                        constant gradient per element
Ce = integral rho cp N^T N dV               consistent or lumped: P18-ELEM-001
K  = sum Ke  (+ convection boundary terms h integral N^T N dA)
Q  = sum of q'' A/3 per facet corner, q''' V/4 per element corner,
     h T_inf A/3 per convection facet corner  (consistent, as P17's loads)
grad(T)_e = G_e T_e ;  q_e = -k grad(T)_e
```

`K` is symmetric positive semi-definite with one null mode on a connected
solid -- the uniform temperature. A prescribed temperature or a convection
surface removes it; without either the model has no datum and the solve must
refuse (RM-THERM-09), never return an arbitrary finite field.

## Unsupported, and refused rather than approximated

```text
radiation                      phase change                thermal contact
anisotropic conductivity       temperature-dependent k, rho, cp
nonlinear boundary conditions  fluid / CFD coupling        thermo-mechanical coupling
moving heat sources            Joule heating               chemical heat generation
adaptive remeshing             multiple-solid assemblies   Tet10 / Hex8
explicit time integration      a second element family    a second material store
```

Twenty behaviours, each a missing feature rather than a wrong number.

## Deferred to a named milestone -- decisions, not scope

```text
consistent vs lumped capacity                   P18-ELEM-001, by measurement
heat-flux and convection sign conventions       P18-LOAD-001, P18-BC-001,
                                                frozen once and shared
the time-varying load / BC policy               P18-TRANSIENT-001
the heat-balance tolerance                      P18-BALANCE-001, measured
the thermal mesh acceptance policy              P18-VALID-001
the sparse solver: extraction or own link       P18-SOLVE-001 (ADR-050)
the four new SI quantity aliases                P18-DATA-001 / P18-LOAD-001
```

## What this scope inherits from P17's findings

F2 and F3 apply to heat flux as they apply to stress: an element-constant
flux on a mesh whose developable walls are never subdivided along their
ruling converges slowly at a flux concentration. RM-THERM-07's refinement
case and any corner-flux case must therefore be convergence studies, and the
suite must assert the trend and the mechanism, not an equality.

## Verification

Scope cannot be proved by a test; what is verified is that the statement and
the code agree. `src/thermal/` holds one translation unit, the input boundary;
no element, no matrix, no DOF, no time step; no linear algebra linked. The
single-solid limit is P16's and the conductivity validation is P15's, each
already tested where it lives.
