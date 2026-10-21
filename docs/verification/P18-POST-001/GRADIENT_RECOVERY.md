# P18-POST-001 — gradient recovery

```text
SUBJECT:  grad(T) = G Te per Tet4: who owns G, how Te is gathered, why the order of the components is [x, y, z]
```

## Who owns what

```text
Tet4 geometry, Jacobian, shape-function gradients, G      P18-ELEM-001  (computeTet4Geometry, Tet4Geometry::gradientFrom)
the gather of Te through the numbering                    P18-DOF-001   (ThermalDofMap::elementDofs)   used here
grad(T) = G Te                                            P18-ELEM-001's published "shared gradient-recovery path"
the Fourier line, the magnitude, the checks               this milestone
```

`ThermalPost.cpp` contains no Jacobian, no determinant, no adjugate and no shape function. Its one call into the geometry is
`table.geometries[e].gradientFrom(local)`. An architecture gate enforces it (see GUI_OWNERSHIP.md, rule 2: the file may not contain a
Jacobian, adjugate, cofactor or shape gradient, and must reach G through `gradientFrom`; the fixture `recovery-rewrites-geometry` proves
the gate fails when it does).

## The gather

For tetrahedron `e` with nodes `[n1, n2, n3, n4]` **in the order the tetrahedron lists them**:

```text
dofs  = map.elementDofs(tet)                   four ThermalDofIndex, in the tetrahedron's own order
Te[i] = temperatures[ dofs[i].value() ]        i = 0..3
geometry = computeTet4Geometry(cornersOf(mesh, tet))     corners in the same order
grad  = geometry.gradientFrom(Te)
```

Corners, unknowns and `Te` therefore share one order, whichever it is: the recovery does not sort the node handles, does not use
`NodeId::value()` as an index, and does not look anything up by position in a global array.

| Property | Test | Mutation that breaks it |
| --- | --- | --- |
| local order is the tetrahedron's, not sorted | `PreservesTetLocalNodeOrderInAMeshWhoseHandlesAreNotSorted`, `NodalTemperatureUsesThermalDofOrderForSparseNodeIds` | PM01 (sorted unknowns): killed by 18 cases |
| the numbering, not the raw NodeId, indexes `T` | sparse-id test, slab on the cylinder | PM02, PM02b (raw id minus one): killed |
| the same tetrahedron named in another even local order gives the same gradient | sparse-id test (second hand mesh) | — |

## The components

`[dT/dx, dT/dy, dT/dz]`, i.e. `TemperatureGradient3{x, y, z}`. Three single-axis fields and one mixed-sign affine field fix it so that no swap and
no transpose of `G` survives:

| Field | Expected grad(T) | Expected q (k = 5) | Test |
| --- | --- | --- | --- |
| `T = x` | `[1, 0, 0]` | `[-5, 0, 0]` | `XLinearFieldGradient` |
| `T = y` | `[0, 1, 0]` | `[0, -5, 0]` | `YLinearFieldGradient` |
| `T = z` | `[0, 0, 1]` | `[0, 0, -5]` | `ZLinearFieldGradient` |
| `T = 307.3 + 7.2x - 3.5y + 11.4z` | `[7.2, -3.5, 11.4]` | `[-36, 17.5, -57]` | `AffineFieldGradientAndFourierFlux` |
| `T = 300 - 0.6x - 0.8y - 2.4z` | `[-0.6, -0.8, -2.4]` | `[3, 4, 12]`, `|q| = 13` | `HeatFluxMagnitudeIsEuclideanAndKeepsTheVector` |

PM03 (y and z swapped) and PM04 (x and y swapped) are killed by 15 and 12 cases. A transposed `G` is the same failure as a component swap for
these fields; the independent oracle below covers it for general tetrahedra.

## The independent oracle

`ThermalPost_GeneralTetIndependentGradientOracle` recovers a scattered nodal field on 162 jittered tetrahedra and compares every gradient with
the test's own dense solve: the 4x4 system `[1 x y z] c = T` (rows are the four corners) is solved by elimination in the test, and `c[1..3]` is the
gradient. Nothing in it calls `G` or the recovery. Measured: maximum `|grad - dense solve|` = **2.45e-13 K/m**.

## Refusals (the whole recovery fails, no element is skipped)

| Condition | Problem | Where it is found |
| --- | --- | --- |
| a tetrahedron P18-ELEM refuses (degenerate, coplanar, inverted, non-finite) | `invalid_tet_geometry`, names the element | `buildElementTable`, before any gradient |
| `G Te` not finite | `non_finite_temperature_gradient` | `fieldsFrom` |
| conductivity not finite and positive | `thermal_conductivity_invalid` | first |
| the numbering does not describe the mesh | `numbering_mismatch` | second |

`OneInvalidTetrahedronFailsTheWholeRecoveryAndIsNeverSkipped` builds two tetrahedra on the same four nodes, `[1 2 3 4]` valid and `[1 3 2 4]` its
inverted mirror: the recovery is refused and names the element; it does not return a one-element field. PM13 (skip the invalid element and
continue) is killed.
