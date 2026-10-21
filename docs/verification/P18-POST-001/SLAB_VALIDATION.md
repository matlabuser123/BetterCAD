# P18-POST-001 — slab validation (sign, magnitude, units, end to end)

```text
SUBJECT:  the one-dimensional conduction slab, recovered from the qualified solver's own field, against the closed form
```

## The closed form

A slab of length `L` along z, `T(0) = Thot`, `T(L) = Tcold`, adiabatic sides, constant `k`, no generation. The solution is linear, so exactly:

```text
dT/dz = (Tcold - Thot) / L        q = [0, 0, -k dT/dz] = [0, 0, k (Thot - Tcold) / L]      |q| = k |Thot - Tcold| / L
```

Heat flows from the hot face toward the cold face. The transverse components `dT/dx`, `dT/dy`, `qx`, `qy` are exactly zero. The expected values
come from this formula; none is read back from a recovery. A linear element represents a linear field exactly, so the discretisation error is zero
on any mesh and what remains is the solver's residual (`1e-9` relative) and rounding.

## Results

Every case is a full chain: document, extrude, P16 mesh, mapping, `ThermalModel`, `ThermalDofMap`, prepared boundary, assembled system, **the qualified
steady solve**, then the recovery.

| Case | Mesh | `Thot -> Tcold` | `L` | `k` | Expected `dT/dz` | Expected `qz` | Worst gradient error | Worst flux error | Worst transverse |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| hand grid, jittered | 4x4x4-node jittered grid, spacing 1/3 m | 400 -> 300 K | 1 m | 45 | -100 K/m | +4500 W/m² | 1.02e-12 K/m | 4.64e-11 W/m² (`|q|` 4.64e-11) | — |
| the same, reversed | same | 300 -> 400 K | 1 m | 45 | +100 K/m | -4500 W/m² | — | 4.37e-11 W/m² | — |
| P16 block | 12 tetrahedra | 400 -> 300 K | 0.02 m | 45 | -5000 K/m | +225 000 W/m² | 7.28e-12 K/m | 3.27e-10 W/m² (`|q|` 3.20e-10) | 3.27e-10 |
| P16 block, reversed | same | 300 -> 400 K | 0.02 m | 45 | +5000 K/m | -225 000 W/m² | — | (checked, bound 1e-2) | — |
| P16 cylinder, coarse | 156 nodes / 605 tetrahedra | 400 -> 300 K | 0.02 m | 45 | -5000 K/m | +225 000 W/m² | 9.31e-10 K/m | 4.19e-08 W/m² | 4.19e-08 |
| P16 cylinder, fine | 900 nodes / 4118 tetrahedra | 400 -> 300 K | 0.02 m | 45 | -5000 K/m | +225 000 W/m² | 3.73e-08 K/m | 1.68e-06 W/m² | 1.68e-06 |
| transient block at `t_end` = 100 s, 16 states | block | 400 -> 300 K | 0.02 m | 45 | -5000 K/m | +225 000 W/m² | — | 1.42e-06 W/m² at the last state | — |

The bounds in the tests are loose against these values on purpose (block: `1e-4` K/m and `1e-2` W/m²; cylinder: `1e-3` K/m and `0.1` W/m²) and are the
same in every preset. The cylinder shows the law holds on two meshes of the same part with different node and element counts (the test requires the
counts to differ), the property a mesh-dependent defect (an ordering or a handle used as an index) would break.

## What the slab settles

| Claim | Evidence |
| --- | --- |
| the sign: `qz > 0` when the bottom is hot | `heatFluxes[0].z > 0` on the block; `< 0` reversed |
| the magnitude `k |dT| / L` | `WithinRel(225000, 1e-7)` on the first element's `|q|` |
| the unit: W/m², not W/mm² | 225 000 W/m² for `k = 45`, `L = 20 mm`: a millimetre mixup (PM10b) is off by 10³ |
| the temperature is recovered in K and found through the numbering | `T(node) = 400 - 100 z / L` to `1e-6` at every node, by `temperatureOf` |
| the flux is element-constant | one value per tetrahedron, all equal to the closed form |
| nothing is smoothed | the transverse error is the rounding of `G Te`, not an average |
| the temperature is the solver's, bit for bit | `==` on every entry |

## The transient limit

At 100 s (many diffusion times of this 20 mm block) the transient history has settled to the slab, so the flux of the last state agrees with the steady one
(max flux error at `t_end` 1.42e-06 W/m², final `|q|` 225 000.000000 W/m²). The earlier states are not slabs and are not asserted to be; they are asserted to
follow the solution's order and times (CURRENTNESS.md, DETERMINISM.md).
