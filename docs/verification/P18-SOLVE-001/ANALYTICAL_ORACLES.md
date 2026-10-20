# P18-SOLVE-001 — analytical and independent oracles

```text
SUBJECT:  every expected temperature, where it comes from, and the proof that the production
          solve was never asked for the answer it is judged against
```

**Independence statement.** Production solve used to compute any expected value: **NO**. Production
reduction used in the oracle: **NO**. `linalg` used in the oracle: **NO**. The expected values are
typed in from the derivations below, or come from `denseExpected` / `denseSolve` in
`tests/thermal/ThermalSolveTestSupport.hpp`: a Gaussian elimination with partial pivoting on a dense
array, written in the test, that gathers the free rows and columns, subtracts the prescribed columns
and eliminates. It shares no code with `ThermalSolve.cpp`.

## Algebraic oracles (the matrix is the model)

| Case | Setup | Expected | Actual |
| --- | --- | --- | --- |
| one free node | `K=[2 -2;-2 2]`, `Q=[0 100]`, `T0=300` | `beff=700`, `T1=300+100/2=350 K` | 350, error 0 |
| nonzero-Dirichlet trap | the same | not 50 K (the answer if `Kfc Tc` is dropped) | 350; with `T0=0` it is 50; shifting `T0` by 10, 100, 273.15 K shifts `T1` by exactly that |
| 3-node chain, symmetric | `g1=g2=1`, `T0=400`, `T2=300`, `Q=0` | midpoint 350 K | 350 to 1e-12 |
| 3-node chain, asymmetric | `g1=2`, `g2=1`, `T0=400`, `T2=300` | `(2*400+1*300)/3 = 366.666...`; with the values exchanged `(2*300+400)/3 = 333.33` | 366.6666666666667 to 1e-9; exchanged case to 1e-11 |
| Robin only | `K=[4]`, `Q=[4*300+40]`, no constraint | `T = T_inf + P/g = 310 K` | 310, residual 0; ambient 250 / 400 / 1000 give 260 / 410 / 1010; doubling `P` doubles the rise |
| Robin-anchored 2-node | `K=[1 -1;-1 2]`, `Q=[10 300]` | `T1 = 310`, `T0 = 320` | to 1e-10 |
| fully constrained | `K=[2 -2;-2 2]`, `Q=[7 -3]`, both held | `T = Tc = [300 350]`, kernel not invoked, `K T - Q = [-107 103]` W | exact |
| pure Neumann | `K=[1 -1;-1 1]`, `Q=0` | refused | `missing_temperature_datum` |
| pure Neumann, incompatible load | the same, `Q=[1 0]` | refused | `missing_temperature_datum` |
| interleaved partition | the 5-unknown chain, `T1=300`, `T4=350`, `Q0=5` | `[305, 300, 8400/26, 8800/26, 350]` | to 1e-10; see DIRICHLET_PARTITION.md |
| random SPD, interleaved | orders 6, 9, 14, 23, every third unknown held | the test's own elimination | relative error 6.2e-16 |

## Finite-element oracles (an assembled model)

| Case | Setup | Closed form | Actual |
| --- | --- | --- | --- |
| unit tetrahedron | `k=6`; corners 0, 1, 2 held at 400, 300, 300 K | free corner 3: `K(3,*) = [-1 0 0 1]`, so `T3 = T0 = 400` | 400 to 1e-10 |
| structured grid 6^3 | 216 unknowns; `T=300` on `x=0`, `400` on `x=L`, adiabatic sides | `T = 300 + 100 x/L` at every node (linear tetrahedra reproduce an affine field exactly) | max error 1.25e-12 K |
| affine patch 5^3 | `a + b x + c y + d z` on every boundary node, 27 interior unknowns | the same field at every interior node | max error 2.84e-13 K |
| real block (P16 mesh) | 400 K on the start cap, 300 K on the end cap, `k=45`, 20 mm | `T = 400 - 100 z/L` at every node | max error 5.7e-14 K |
| real block, Dirichlet in series with convection | 400 K start cap; `h=25`, `T_inf=293.15 K` end cap | `q = (400 - T_inf)/(L/k + 1/h) = 2641.9 W/m2`, `T(z) = 400 - q z/k`, end face 398.825824 K | max error 1.7e-13 K |
| real block, convection only | 2.5 MW/m^3 generation (60 W), `h=25`, no Dirichlet | the heat leaving through the film equals the heat generated | 60 W vs 60 W to 1e-8 relative |
| tetrahedron, convection only | 24 W/m^3 (4 W) in the unit tet, `h=4`, face area 1/2 | `h A (mean face T - T_inf) = 4 W` | to 1e-9 |
| fine cylinder (P16 mesh) | 350 K on one cap, convection on the other | maximum principle: every `T` in `[293.15, 350]` | `[349.375, 350.000]`... see SPARSE_SOLVE below |

The convection-only and cylinder rows check the SOLUTION against a conservation statement and a bound
that the solve never saw; they are validations of the solved field, not the balance milestone.

## What these do NOT claim

The reaction heat read from the full residual of the two-face block (270 W through the cold face,
`k A dT / L = 270 W`) is printed as a plausibility measurement and is not qualified here. Heat flux,
gradient and balance are later milestones'; the full residual is retained for them.