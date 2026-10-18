# P18-LOAD-001 — units, and the tolerance table

```text
SUBJECT:  every quantity's unit, the dimensional proof, and the numerical
          class each comparison belongs to
```

## The units

| Quantity | Unit | Type |
| --- | --- | --- |
| surface heat flux `q''` | W/m^2 | `HeatFlux` |
| volumetric generation `q'''` | W/m^3 | `VolumetricHeatSource` |
| P16 node coordinates | m | `Length` (`Point3D`, SI) |
| triangle area | m^2 | `Area` |
| Tet4 volume | m^3 | `Volume` |
| nodal heat input `Q` | W | `Power` |
| total heat rate | W | `Power` |

No W/mm^2, no W/cm^2, no viewer-millimetre coordinate and no factor of
`1000` appears in `ThermalLoadVector.{hpp,cpp}` or `ThermalTarget.{hpp,cpp}`
(grep in [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)). A flux written in
other units converts at construction: `1 W/mm^2` is exactly `1e6 W/m^2`.

## The dimensional proof is a `static_assert`

```text
q'' . A     W/m^2 . m^2   =  W     decltype(HeatFlux{} * Area{})::dimension == power
q''' . V    W/m^3 . m^3   =  W     decltype(VolumetricHeatSource{} * Volume{})::dimension == power
```

and a heat flux, a volumetric source and a power are three different
dimensions, so none can be handed where another is wanted. Eight compile-fail
cases enforce it (`a-heat-flux-is-not-a-generation`, `a-power-is-not-a-heat-flux`,
`a-heat-flux-is-not-a-double`, ...). Each nodal entry is therefore a `Power`,
which is what makes the later `K [W/K] T [K] = Q [W]` and
`C [J/K] dT/dt [K/s] = W` agree.

## Geometry scaling, the test a hidden unit factor fails

```text
x' = s x      surface heat Q' = s^2 Q      volume heat Q' = s^3 Q
s = 1e-3, 0.1, 2, 10, 100      asserted to 1e-14 (surface) and 1e-13 (volume)
```

A viewer-millimetre geometry would scale by `1e6` and `1e9`; mutations L09 and
L10 (a `1e6` on the area, a `1e9` on the volume) are killed.

## The tolerance table (brief section 190: one class per row, no one loose tolerance)

| Metric | Numerical class | Measured | Tolerance | Reason |
| --- | --- | --- | --- | --- |
| single-triangle integration (3 m^2, 12 W/m^2) | algebraic, exact in binary | 0 | exact `==` | `12*3/3` and the sums are exactly representable |
| general-triangle integration | algebraic | below 1e-14 | 1e-14 rel | one product, one division, a cross product |
| single-Tet generation (V = 1/6, q''' = 24) | algebraic | below 1e-15 | 1e-15 rel | `1/6` is not exact in binary |
| nodal sum against `q'' sum(A_f)` (mapped face) | algebraic accumulation | 0 | 1e-13 rel | a few ulps over a few facets |
| nodal sum against `q''' sum(V_e)` (block) | algebraic accumulation | 0 | 1e-13 rel | likewise over 12 tetrahedra |
| general-tet integration (V = 0.7825, q''' = 18) | algebraic | below 1e-13 | 1e-13 rel | a determinant, a division by 6, a division by 4 |
| per-node value against the test's own integration, surface | algebraic | below 1e-14 | 1e-14 rel | same arithmetic, written independently in the test |
| per-node value against the test's own integration, volume | algebraic | below 1e-13 | 1e-13 rel | likewise |
| superposition, combined against the sum of the parts | algebraic | below 1e-13 | 1e-13 rel (mixed), 1e-14 rel (same-kind) | addition of the same shares |
| surface signed cancellation | exact | 0 | `== 0` | two terms per node |
| volume signed cancellation | accumulation | 0.83 eps | 8 eps of the entries | `a+b+c-a-b-c` rounds across many tetrahedra |
| mesh area against CAD, planar block cap | mesh fidelity | 0 | 1e-12 rel | two triangles tile a rectangle exactly |
| mesh volume against CAD, block | mesh fidelity | 0 | 1e-12 rel | tetrahedra tile a prism of planar faces |
| mesh cap area against CAD, cylinder, coarse | **geometric approximation** | -0.507 % | < 10 % | a polygon inscribed in a circle |
| mesh cap area against CAD, cylinder, fine | geometric approximation | -0.032 % | not worse than coarse | same, refined |
| cross-mesh total, coarse against fine | geometric approximation | 0.475 % | the coarse mesh's own error + 1e-12 | the totals differ by the meshes' discretisation, nothing else |
| scaling `Q'/Q` against `s^2`, `s^3` | algebraic | below 1e-13 | 1e-14 / 1e-13 | exact power law |

The two classes that are most easily confused are kept in separate rows and
separate assertions: **load integration** (`sum Q == q'' sum A_f`, a few ulps)
and **mesh representation** (`sum A_f == A_CAD`, planar exact, curved a
discretisation). The load conversion is never blamed for meshing's
approximation, and meshing's approximation is never hidden in a loosened
conversion tolerance.
