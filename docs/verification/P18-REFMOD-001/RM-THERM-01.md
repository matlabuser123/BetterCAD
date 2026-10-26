# RM-THERM-01 — Single-tetrahedron and affine-patch reference

**The strongest single test of the element.** A linear tetrahedron interpolates an affine field exactly, so for `T = 287.25 + 7.2 x − 3.5 y + 11.4 z` (K, m) it must give `∇T = (7.2, −3.5, 11.4)` K/m in every element, `q = −k ∇T = (−36, +17.5, −57)` W/m² (k = 5 W/(m K)), nodal temperatures that *are* the field, and — on a mesh with an **interior node** — an interior node that the solve puts exactly on the plane when the boundary nodes are held to it. The coefficients are all different and none is zero, so a permuted component, a transposed gradient or a sign error cannot pass; the skew tetrahedron is not axis-aligned, so a Jacobian indexed the wrong way cannot pass either.

## Fixtures

* **unit** tetrahedron `(0,0,0) (1,0,0) (0,1,0) (0,0,1)`: volume `1/6`.
* **skew** tetrahedron `(0,0,0) (1.2,0.1,0) (0.2,1.1,0.15) (0.1,0.3,0.9)`: `6V = 1.1175`, worked by hand (`1.2 × 0.945 + 0.1 × (−0.165)`). Positive determinant.
* **`ThermalPatch`**: 40 × 30 × 20 mm block, 20 mm target → **9 nodes, 12 tets, 8 boundary nodes + 1 interior node**.

## Oracle

`ThermalAnalytic.hpp`: `Affine`, `Tet` (shape gradients from the 4 × 4 inverse of `[1 x y z]`, signed volume, `Ke = k V GᵀG`) — shares nothing with `computeTet4Geometry` / `computeTet4Conduction`.

## Tests (8) and what each shows

| Case | Shows | Measured |
|---|---|---|
| `FixturesAreWhatTheyClaimToBe` | oracle volumes/determinants equal the hand values | exact |
| `AffineNodalInterpolation` | barycentric weights `δij` at the corners; interpolant at interior points equals the field | `kClassA` |
| `ConstantGradient` | `∇T = (7.2, −3.5, 11.4)` on both tets | 7.1999999999999886 / 7.1999999999999647 |
| `FourierFlux` | `q = −k ∇T` component by component, and `|q|` | `kClassA` |
| `ElementMatrixIndependentOracle` | production `Ke` equals the dense oracle entry by entry (`≤ 1e-12 × scale`), **exactly** symmetric, row sums 0, rank 3, positive semi-definite | max entry error **0** (unit, scale 2.5), **4.4·10⁻¹⁶** (skew, scale 1.57) |
| `EnergyConsistency` | `Teᵀ Ke Te = k V |∇T|²` | unit 161.70833333334895 vs 161.70833333333331; skew 180.70906249999371 vs 180.70906250000004 |
| `NegativeOrientationIsRefusedNotCorrected` | an inverted tetrahedron is **refused**, not silently re-oriented | refusal |
| `PatchOnAMeshWithAnInteriorNode` | boundary nodes imposed from the field through the constraint seam, the interior node **solved**: lands on the plane; every element's gradient/flux equals the field's; reactions equal the oracle's `K T` at boundary nodes and `0` at the interior node; reactions sum to 0 | interior `T` 287.45549987439267 vs 287.45549987439256; worst gradient error **8.7·10⁻¹² K/m**; flux **4.4·10⁻¹¹ W/m²**; worst reaction error **1.0·10⁻¹⁴ W** |

## Notes

The patch is imposed *through the algebraic seam* (`buildThermalConstraintMap`, `solveSteadyThermalSystem`, `recoverSteadyFromSolution`) because no canonical boundary condition can say "a different temperature at every node". That is the same production assembly, solve and recovery the document-level runs use; only the boundary data is supplied differently. The document `ThermalPatch` also solves end to end (400 K / 300 K caps) so it runs through the runner and the CLI.
