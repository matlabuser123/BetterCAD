# P18-BC-001 — independent analytical validation

```text
SUBJECT:  every number the milestone's mathematics produces, against a value
          written down by hand, with the production code never used as its own oracle
production Kconv used as oracle:  NO
production Qconv used as oracle:  NO
production area used as oracle:   NO   (the test's own cross product)
```

The expected values are typed into the tests as literals (`2 1 1`, `1200`, `hA/12`
by arithmetic on the inputs), or computed from the mesh coordinates by a cross
product written in the test. The only production inputs an expectation shares are
P16's facet SETS: which triangles lie on a CAD face is P16's question, and those
are checked against the CAD face's known area.

## The hand triangle

```text
nodes (0,0,0) (2,0,0) (0,3,0)      A = 1/2 |(2,0,0) x (0,3,0)| = 3 m^2
h = 4 W/(m^2 K)      T_inf = 300 K
h A / 12 = 4 * 3 / 12 = 1 W/K          h T_inf A / 3 = 4 * 300 * 3 / 3 = 1200 W
```

| | Expected (by hand) | Actual | Error |
| --- | --- | --- | --- |
| `Kconv` | `[2 1 1; 1 2 1; 1 1 2]` W/K | `[2 1 1; 1 2 1; 1 1 2]` W/K | 0 (exact) |
| `Qconv` | `[1200 1200 1200]` W | `[1200 1200 1200]` W | 0 (exact) |
| `sum Qconv` | `h T_inf A` = 3600 W | 3600 W | 0 |

## The traps

| Wrong matrix | Entries | Row sums | Rejected by the oracle? |
| --- | --- | --- | --- |
| centroid (one-point) | `hA/9 = 4/3` everywhere | `hA/3 = 4` (correct) | **yes** |
| lumped | `hA/3 I = 4 I` | `hA/3 = 4` (correct) | **yes** |
| negated | `-[2 1 1; ...]` | `-4` | yes |
| times 12/9 (wrong divisor) | `[8/3 4/3 4/3; ...]` | `16/3` | yes |
| times a conductivity (45) | `45 [2 1 1; ...]` | `180` | yes |

The centroid and lumped matrices have the correct row sums; a conservation or
row-sum test cannot see them. The tests compare every entry, and the same
comparison accepts the production matrix.

**Centroid quadrature trap protected: YES. Lumped Robin trap protected: YES.**

## Equilibrium and sign

For uniform facet temperatures, `Kconv T - Qconv` and its sum:

| `T_b` | Expected | Actual (sum) |
| --- | --- | --- |
| 300 K (`= T_inf`) | `[0 0 0]`, sum 0 | `[0 0 0]`, 0 W (exact) |
| 350 K (`> T_inf`) | `h A (T_b - T_inf) = 4 * 3 * 50 = +600 W` outward | `[200 200 200]`, +600 W |
| 250 K (`< T_inf`) | `-600 W`: the ambient heats the body | `[-200 -200 -200]`, -600 W |
| `[300 350 250]` K | row sums equal, so the mean 300 K gives 0 | `[0 +50 -50]`, 0 W |

## A general triangle

`(0.3, -0.2, 0.5)`, `(1.7, 0.4, 0.9)`, `(0.1, 1.3, -0.4)`: area 1.38025359988663 m²
by the test's cross product; `h = 17.5`, `T_inf = 288.15`. Every `Kconv` entry
equals `h A / 12 (2 or 1)` and every `Qconv` entry `h T_inf A / 3` to 1e-14 relative;
the constant-ambient residual is below 1e-13 of `h A T_inf / 3`.

## Scaling

| Test | Expected | Result |
| --- | --- | --- |
| geometry `x' = s x`, `s = 1e-3, 0.1, 2, 10, 100` | `Kconv`, `Qconv`, `A` by `s^2` | within 1e-13 |
| `h -> c h`, `c = 0.25, 2, 7.5, 1e3` | both by `c` | within 1e-15 |
| `T_inf` over 0 to 1e4 K | `Qconv` by `T_inf / 300`, `Kconv` UNCHANGED | `Kconv` bit-identical |
| `T_inf = 0 K` | `Qconv = 0`, `Kconv` remains | exact |
| `h = 0` | all zeros for any `T_inf` | exact |

## A multi-facet face

A 2 × 3 rectangle split into two triangles (test-side accumulation, `h = 4`,
`T_inf = 300`): a shared node's diagonal entry is 4 (2 from each facet), an
unshared node's 2, the shared edge's coupling 2 and each outer edge's 1, the total of
all entries `h A = 24 W/K`, and `sum Q = 7200 W = h T_inf A`. An overwrite
instead of a sum would give 2 at the shared nodes.

## The mapped block end cap (planar)

```text
CAD area  40 mm x 30 mm = 1.2e-3 m^2       mapped facets 2, unique nodes 4 of 9
h = 25 W/(m^2 K)    T_inf = 293.15 K
mesh area vs CAD            exact to 1e-12 (two triangles tile a rectangle)
sum Kconv = h A             3.0e-2 W/K  to 1e-13
sum Qconv = h T_inf A       8.7945 W    to 1e-13
T_b = 293.15 K   sum(Kconv T - Qconv) =  0 W              (expected 0)
T_b = 350 K      sum(Kconv T - Qconv) = +1.7055 W        (expected +1.7055)
T_b = 250 K      sum(Kconv T - Qconv) = -1.2945 W         (expected -1.2945)
dense system against the test's own expectation   within 1e-13 of the largest entry
```

## The cylinder cap, coarse and fine (curved: a mesh-fidelity case)

The two meshes genuinely differ (asserted before anything is compared):

```text
                 nodes   boundary facets   cap facets   cap nodes   mesh cap area (m^2)   vs CAD
coarse            156          140              34           36       1.12524019e-4        -0.507 %
fine              900          572             142          144       1.13061452e-4        -0.032 %
CAD (pi r^2)                                                            1.13097336e-4
```

| Quantity at `T_b = 350 K`, `h = 25`, `T_inf = 293.15 K` | Coarse | Fine | CAD `h A (T_b - T_inf)` |
| --- | --- | --- | --- |
| convective total (W) | 0.159924762 | 0.160688589 | 0.160739588 |

The prescribed temperature maps to 36 nodes on the coarse mesh and 144 on the
fine one, every one at exactly the same `T_bar`.

**Two tolerance classes, separately.** Against its own mesh each total is exact to
1e-12: that is the integration. Against the CAD figure each carries the mesh's
representation error: the fine mesh is no worse than the coarse, the coarse is
within 10 %, and the two totals differ by no more than the coarse mesh's own
error. The conversion is never blamed for meshing's approximation, and meshing's
approximation is never hidden in a loosened conversion tolerance.
