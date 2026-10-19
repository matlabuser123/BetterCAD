# P18-BC-001 — units, and the tolerance table

```text
SUBJECT:  every quantity this milestone touches, its unit, and how close
          "equal" has to be, one class per row
```

## Units

| Quantity | Type | Unit | Canonical? |
| --- | --- | --- | --- |
| prescribed temperature `T_bar` | `Temperature` | K | yes |
| ambient temperature `T_inf` | `Temperature` | K | yes |
| convection coefficient `h` | `HeatTransferCoefficient` | W/(m² K) | yes |
| facet area `A` | `Area` | m² | derived |
| `Kconv` entry | `ThermalConductance` | W/K | derived |
| `Qconv` entry | `Power` | W | derived |
| `Kconv T` | `Power` | W | derived |
| `T` in the residual | `Temperature` | K | derived |

Absolute kelvin only. There is no Celsius anywhere in the tree; 350 K is stored as
350, not 76.85, and `ThermalBC_PrescribedTemperatureDefinition` asserts it. A
temperature below 0 K, a NaN and an infinity are refused wherever one is accepted,
by the one function (`isAbsoluteTemperature`) P18-DATA-001 wrote.

## Checked by the compiler

```text
h A              W/(m^2 K) . m^2       = W/K     == ThermalConductance
h T_inf A        W/(m^2 K) . K . m^2   = W
(h A) T          (W/K) . K             = W
h is not k       HeatTransferCoefficient != ThermalConductivity
```

Eight of the 31 compile-fail cases pin the distinctions: a coefficient is not a
double, a conductivity or a temperature; a temperature is not a double; the
ambient is not a coefficient; a conductivity is not an argument of the kernel; a
conductance entry is not a power and a power is not a conductance entry.

## Geometry scaling, the test a hidden unit factor fails

```text
x' = s x      area A' = s^2 A      Kconv' = s^2 Kconv      Qconv' = s^2 Qconv
s = 1e-3, 0.1, 2, 10, 100      asserted to 1e-13 relative
```

A viewer-millimetre geometry would scale by `1e6`; mutation B22 (a `1e6` on the
area) is killed.

## The tolerance table (one class per row, no one loose tolerance)

| Metric | Numerical class | Tolerance | Reason |
| --- | --- | --- | --- |
| reference triangle `Kconv`, `Qconv` (A = 3, h = 4, T = 300) | algebraic, exact in binary | exact `==` | `h A / 12 = 1` and `1200` are representable |
| general triangle `Kconv`, `Qconv` against the formulas | algebraic | 1e-14 rel | one product, one quotient |
| constant-ambient residual, general triangle | algebraic cancellation | 1e-13 of `h A T_inf / 3` | the two terms are equal to a few ulps |
| symmetry of `Kconv` | structural | bitwise | no symmetrisation exists |
| eigen-action of `Kconv` (`hA/3`, `hA/12`) | algebraic | 1e-15 abs | exact entries, small integers |
| scaling by `s^2`, `c`, `T_inf` | algebraic | 1e-13, 1e-15, 1e-14 rel | exact power laws; `T_inf` leaves `Kconv` bit-identical |
| prescribed value in the constraint map | exact canonical double | exact `==` | no arithmetic touches it |
| conflict between two prescriptions | exact canonical equality | none | `300` and `nextafter(300)` conflict; no solver tolerance |
| mapped face: `sum(Kconv) = h sum(A_f)`, `sum(Qconv) = h T_inf sum(A_f)` | algebraic accumulation | 1e-13 rel | a few ulps over a few facets |
| dense mapped system against the test's own expectation | algebraic | 1e-13 of the largest entry | independent arithmetic |
| mapped planar cap against the CAD area | mesh fidelity | 1e-12 rel | two triangles tile a rectangle exactly |
| equivalent single `(h1 + h2, T_eff)` against two conditions | algebraic | 1e-12 of the largest entry | different association of the same sums |
| cylinder cap, coarse and fine, against CAD | **geometric approximation** | fine not worse than coarse; coarse < 10 % | a polygon inscribed in a circle |
| cross-mesh convective total | geometric approximation | the coarse mesh's own error + 1e-12 | the totals differ by discretisation, nothing else |
| stale / current, source identity | discrete | exact | a value, not a size or a hash |

The two classes most easily confused are kept in separate rows and separate
assertions: **boundary-term integration** (`sum Qconv == h T_inf sum A_f`, a few
ulps) and **mesh representation** (`sum A_f == A_CAD`, exact on a plane, a
discretisation on a curve). The conversion is never blamed for meshing's
approximation, and meshing's approximation is never hidden in a loosened
conversion tolerance.
