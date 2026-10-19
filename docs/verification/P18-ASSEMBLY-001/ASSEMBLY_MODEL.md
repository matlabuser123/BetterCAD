# P18-ASSEMBLY-001 — the assembled model

```text
SUBJECT:  exactly what the global system is, which milestone owns each term of it,
          and what assembly is allowed to do
```

## The system

For the linear Tet4 conduction model the assembled, **unconstrained** global system is

```text
steady       K T = Q
transient    C dT/dt + K T = Q

K  =  sum over Tet4  Ke   +   sum over convection facets  Kconv        W/K   sparse, symmetric
C  =  sum over Tet4  Ce                                                J/K   sparse, symmetric, transient only
Q  =  prepared heat input   +   sum over convection facets  Qconv      W     dense, length Ndof
```

`Ndof` is `ThermalDofMap::dofCount()`, which is the number of mesh nodes. `K` and `C`
are `Ndof x Ndof`; `Q` is `Ndof` long. `K T` and `C dT/dt` are both powers (a
`static_assert` in `ThermalSystem.hpp` proves it), so both equations are equations
between watts.

## Who owns what

Assembly is **scatter, not physics**. Every number that is scattered comes from a
milestone that qualified it, and this milestone repeats none of the formulas.

| Term | Owner | Entry point consumed |
| --- | --- | --- |
| `Ke = k V G G^T`, `Ce = (rho cp V / 20) [2 1 1 1; ...]` | P18-ELEM-001 | `computeTet4Geometry`, `computeTet4Conduction`, `computeTet4Capacity` |
| surface `q''` to nodal watts, volume `q'''` to nodal watts | P18-LOAD-001 | `PreparedThermalLoads::heatInput()` |
| `Kconv = (h A / 12) [2 1 1; 1 2 1; 1 1 2]`, `Qconv = (h T_inf A / 3)` | P18-BC-001 | `PreparedConvectionBoundary::facets`, `ConvectionFacet::term` |
| prescribed temperatures | P18-BC-001 | not touched here (see below) |
| material resolution, steady needs `k`, transient needs `rho` and `cp` | P18-MAT-001 | `resolveThermalMaterial` |
| the numbering, `NodeId` to unknown | P18-DOF-001 | `ThermalDofMap::elementDofs`, `dofOf`, `describes` |
| element order | P16 | `Mesh::tetrahedra()`, ascending `ElementId` |

A source audit (ADVERSARIAL_REVIEW.md, question 6) confirms the file contains no `V *
k`, no `/ 20`, no `/ 12`, no `/ 3`, no `/ 4` and no `q'' A`.

## What assembly is allowed to do

1. check that its inputs are current and describe each other (SOURCE_COMPATIBILITY.md);
2. walk the elements and the convection facets in a frozen order;
3. add each local entry into exactly one global slot;
4. check the result is finite and symmetric;
5. record what it was built from and publish all of it at once, or nothing.

## What it must not do, and does not

- **No Dirichlet handling.** Prescribed-temperature unknowns keep their rows and
  columns. `K_ff T_f = Q_f - K_fc T_c` needs `K_fc`, and a reaction heat needs the
  constrained rows. Applying a value is P18-SOLVE-001's job, exactly, by elimination.
  A test assembles the same case with and without a prescribed temperature and
  compares **bit-identical** matrices and `Q`.
- **No regularisation.** No penalty, no `epsilon` on a diagonal, no pinned unknown, no
  spring. A pure-Neumann `K` is singular; `K 1 = 0` is the invariant of a correct
  conduction matrix and is tested to `2.2e-16` on the real block.
- **No symmetrisation, no pruning.** The symmetry check is a gate that refuses; it
  never repairs. Exact zeros stay in the pattern, which is therefore a function of
  connectivity alone.
- **No `C / dt`, no solve, no residual, no reaction, no result.** `dt` is not an input.
  The compile-fail group proves none of those members exists.

## The steady and transient contract

| | steady | transient |
| --- | --- | --- |
| material needs | `k` | `k`, `rho`, `cp` |
| `K` | assembled | assembled |
| `C` | **absent** (`capacity()` is empty) | assembled |
| a material with `k` only | assembles | **refused**, `material_unavailable` |

"Absent" is `std::optional` being empty, never a zero matrix: a zero `C` that a
transient solver would treat as a massless body is the failure this design makes
unrepresentable. Measured: a `k`-only cylinder assembles steady with no `C` and is
refused as `material_unavailable` under a transient mode.

## The convection source is not the exchanged heat

`Q` carries the **ambient part** of the Robin term, `h T_inf A`. The heat actually
leaving through the convective boundary is `Kconv T - Qconv` and needs a solution. So
`ThermalHeatVector::total()` is the total the right-hand side carries and is documented
as such; it is not a heat balance. On the real block (`h = 25`, `T_inf = 293.15 K`, a
0.0012 m^2 end cap carrying 500 W/m^2, and a 2.5 MW/m^3 generation over 2.4e-5 m^3) the
vector carries 0.6 W + 60 W + 8.7945 W = 69.3945 W, of which 8.7945 W is the ambient
part. See ANALYTICAL_VALIDATION.md.