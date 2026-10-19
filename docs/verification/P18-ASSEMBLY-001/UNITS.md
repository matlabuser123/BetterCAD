# P18-ASSEMBLY-001 — units and tolerances

```text
SUBJECT:  the unit of every assembled quantity, the checks that prove them, and the
          tolerance of each class of assertion
```

| Quantity | Unit | Type |
| --- | --- | --- |
| `K` entries | W/K | `ThermalConductance` in `ConductanceMatrix` |
| `C` entries | J/K | `HeatCapacity` in `CapacityMatrix` |
| `Q` entries | W | `Power` in `ThermalHeatVector` |
| `k`, `rho`, `cp` | W/(m K), kg/m^3, J/(kg K) | consumed through the kernels |
| `h`, `T_inf` | W/(m^2 K), K | consumed through the prepared boundary |

Compiler-checked: `K T` is `(W/K) K = W`; `C dT/dt` is `(J/K)(K/s) = W`; conductance and
capacity dimensions differ and the two matrix types are distinct. A conductance entry
cannot be assigned to a power, a capacity entry or a double (compile-fail, twelve
conversion cases).

## Tolerance classes

Every assertion is in exactly one class; none was loosened to pass.

| Class | Bound | Used for |
| --- | --- | --- |
| exact | `0.0` / `==` | symmetry of the assembled matrices (the scatter writes the same numbers into both triangles in the same order), bitwise repeatability, element order, patterns, counts |
| scatter | `1e-14` absolute on O(1) matrices; `1e-12` relative on mapped meshes | comparison against the independent dense oracle (the only rounding is the order of a sum) |
| resultant | `1e-12` to `1e-13` relative | `Q` total against the independent expectation; `1^T C 1` against `rho cp V` |
| null mode | `1e-12 * max|K|` | `K 1 = 0` (measured `2.2e-16` on the real block, `0` on the hand meshes) |
| energy identity | `1e-11` relative | `T^T K T` against the sum of local energies (measured identical to 12 digits) |
| symmetry gate (production) | `1e-13 * max|K|` | refusal threshold; a correct scatter is exactly symmetric, so the bound is not tight to anything |
| analytic hand cases | `0.0` | the unit tetrahedron's `K` and `C` have entries that are small integers |