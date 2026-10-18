# P18-LOAD-001 — volumetric heat generation

```text
SUBJECT:  q''' [W/m^3] in the analysed solid -> an equivalent nodal heat input [W]
SOURCE:   thermal::integrateVolumetricHeatSource / elementNodalHeat, driven by
          meshing::regionOf and P18-ELEM-001's computeTet4Geometry
```

## The integral

For a four-node linear tetrahedron of volume `V` under a constant `q'''`:

```text
Qe_i = integral_V N_i q''' dV = q''' V / 4        i = 1 .. 4
sum_i Qe_i = q''' V
```

(`integral_V N_i dV = V/4`: a linear function integrates to the volume times its
value at the centroid, where each `N_i = 1/4`.) It is exact. NOT `q''' V / 3`,
NOT `q''' V` per node; mutations L03 and L04 are killed.

## The volume is P18-ELEM-001's, and it is the SIGNED volume of a VALID element

`computeTet4Geometry` is called for each tetrahedron and nothing else from the
element kernel: **no `Ke`, no `Ce`**. It supplies the gate (finite, not
degenerate under the derived noise bound, positively oriented) and the signed
volume `det J / 6` of the validated element. An inverted element is refused as
`InvalidElement` and is **not** integrated on `|V|`; mutation L40 replaces the
gate and the volume by `abs(signedVolume)` and is killed.

## The target is the whole analysed solid

`VolumetricHeatSourceLoad` has no target field (P18-DATA-001, ADR-049: one
solid). The elements are `meshing::regionOf(map, mesh)`: every Tet4 of the
mapped body, ascending, deduplicated, whose `source` is the analysed body. No
`ElementId` is stored anywhere canonical, and a sub-volume has no
representation at all (`the-volumetric-load-has-no-face` and `...-no-element`
are compile errors).

## Hand oracle 1 — the unit tetrahedron

```text
nodes   (0,0,0) (1,0,0) (0,1,0) (0,0,1)         V = 1/6 m^3
q'''    24 W/m^3
expected total  q''' V = 4 W        expected per node  1 W each
actual          4 W (rel 1e-15)     actual             [1 1 1 1] W
```

## Hand oracle 2 — the brief's non-orthogonal tetrahedron

```text
nodes   (0.2,-0.4,0.1) (1.7,0.3,0.2) (0.4,1.5,0.6) (0.1,0.2,2.0)
V       0.7825 m^3                 by the test's own determinant (4.695 / 6)
q'''    18 W/m^3
total   14.085 W        each corner 3.52125 W        error below 1e-13 relative
```

## Accumulation at shared nodes

Two tetrahedra on the common face (nodes 2, 3, 4; volumes 1/6 and 1/3 m^3): the
shared nodes receive `q''' (V1 + V2) / 4`, the apexes `q''' V_i / 4`. The unit
cube cut into six Kuhn tetrahedra around its diagonal (total volume exactly
1 m^3) at `q''' = 90` gives 90 W, and the two corners every tetrahedron touches
receive `6 * (90/6)/4 = 22.5 W` each.

## The whole body, through P16

```text
body            40 x 30 x 20 mm block      12 tetrahedra, 9 nodes
CAD volume      0.040 * 0.030 * 0.020 = 2.4e-5 m^3
mesh volume     2.4e-5 m^3 by the test's determinants (rel error 0)
q'''            2.5e6 W/m^3
prepared total  60 W         expected q''' V_CAD = 60 W        sum error 0
region          source == the analysed body; 12 elements == every tetrahedron
```

Two comparisons, two tolerances, never conflated: the **conversion**
(`sum Q == q''' sum V_e`, algebraic, 1e-13) and the **mesh**
(`sum V_e == V_CAD`, exact for a prism of planar faces, 1e-12).

## Scaling

```text
generation  q''' -> c q'''   Q -> c Q       c = 0.25, 2, 10, -3        1e-15
geometry    x' = s x         Q' = s^3 Q     s = 1e-3, 0.1, 2, 10, 100  1e-13
```

## No material

A prescribed `q'''` needs no `k`, `rho` or `cp`. The preparation takes no
material; a conductivity and specific-heat edit leaves the prepared vector
**bit-identical** (`VolumeGenerationIsMaterialIndependent`,
`MaterialEditDoesNotInvalidatePreparedGeometryTarget`).
