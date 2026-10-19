# P18-ASSEMBLY-001 — load resultants

```text
SUBJECT:  the heat the right-hand side carries, by category, against independent
          expectations
```

Assembly adds the prepared heat input **once** and adds the convection ambient source
once. `PreparedThermalLoads` also keeps per-load contributions; they are *attribution*
of the same vector and are not added again (a mutation that would add both is in
MUTATIONS.md).

## Real block (40 x 30 x 20 mm, `V = 2.4e-5 m^3`)

| Category | Expected | `Q` carries |
| --- | --- | --- |
| surface flux `q'' A`, 500 W/m^2 on a 0.0012 m^2 end cap | 0.6 W | 0.6 W (prepared loads, `1e-12` relative) |
| generation `q''' V`, 2.5 MW/m^3 | 60 W | 60 W |
| convection ambient source `h T_inf A`, `h = 25`, `T_inf = 293.15 K` | 8.7945 W | 8.7945 W |
| **combined** | **69.3945 W** | **69.3945 W**, `|error| <= 1e-13` relative |

Each category was also assembled alone (flux only, generation only, convection only,
loads without a boundary): `0.6`, `60`, `8.7945`, `60.6` W, each within `1e-13` relative.

Asserted explicitly: the combined `Q` total equals `PreparedThermalLoads::totalHeatRate +
PreparedThermalBoundary::convectionSource`, and is below `1.5 *` the expectation (a
double count would be about `2 *`).

## Hand mesh

One tetrahedron with `q''' V = 12 W`, a surface flux of 5 W and a convection source of
600 W: 617 W expected, 617 W actual.

## What this is not

The convection term is the **ambient** part, `h T_inf A`. It is not heat entering the
body; heat balance needs the solved field (`Kconv T - Qconv`) and belongs to
P18-BALANCE-001. `ThermalHeatVector::total()` says so in its declaration.