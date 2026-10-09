# P17-POST-001 — result conventions, units and ownership

```text
RESULT: PASS
```

Everything here is **consumed** from P17-DATA-001 and P17-ELEM-001, not
redefined. That is the point of the milestone: the conventions were frozen
before the code that reads them back existed, and this file records that
nothing moved.

## The frozen conventions, and where they live

```text
Voigt order          structural::TensorComponent        P17-DATA-001
                     XX=0 YY=1 ZZ=2 XY=3 YZ=4 ZX=5

strain vector        structural::Strain6                P17-DATA-001
                     [ xx yy zz gammaXy gammaYz gammaZx ]

engineering shear    IN THE FIELD NAMES, not a comment:
                     gammaXy = 2 epsilonXy
                     gammaYz = 2 epsilonYz
                     gammaZx = 2 epsilonZx

stress vector        structural::Stress6                P17-DATA-001
                     [ xx yy zz xy yz zx ], each a Stress (= Pressure)

B                    Tet4Kinematics::strainFrom         P17-ELEM-001
D                    ElasticityMatrix::stressFrom       P17-ELEM-001
local DOF order      localDofIndex(node, component)     P17-ELEM-001
global DOF order     ADR-037, via elementDegreesOfFreedom
```

P17-DATA's own header states the reason the order is frozen there rather than
here, and it names this milestone:

```text
"FROZEN HERE, ONCE, FOR EVERY P17 MILESTONE. P17-ELEM-001 builds a 6x12 B
 matrix and a 6x6 D matrix whose rows are in this order; P17-POST-001 reads
 them back [...] A module that chose its own would produce stress that looked
 plausible and was wrong in the shear terms only -- which is the hardest kind
 of defect to see."
```

And P17-ELEM's header states why `strainFrom` and `stressFrom` are public:

```text
"B AND D ARE PUBLIC ON PURPOSE. `P17-POST-001` recovers strain as
 `eps = B u_e` and stress as `sigma = D eps`, and if B were buried in this
 translation unit that milestone would reimplement it -- with its own
 transpose and its own shear convention."
```

So the shared path was designed two milestones ago. This one used it.

## Reuse, counted rather than claimed

Counts over `src/structural/StructuralPost.cpp` and
`include/bettercad/structural/StructuralPost.hpp`:

```text
pattern                                occurrences   where
-------------------------------------------------------------------------
computeTet4Kinematics                       1        recoverOneElement
->strainFrom / .strainFrom                  1        recoverOneElement
.stressFrom                                 1        recoverOneElement
isotropicElasticity                         1        run(), once per model
elementDegreesOfFreedom                     1        elementDisplacements
localDofIndex                               3        elementDisplacements

a second B or shape gradient                0
"1.0 / (6" or a cofactor determinant        0
"lambda" as an identifier                   0
"E * nu" or "2 * (1 + nu)"                  0
"poissonRatio" / "youngsModulus"            0
a second Voigt index table                  0
```

`B` is reached once, `D` once, and the twelve global indices once -- through
P17-ASSEMBLY's own `elementDegreesOfFreedom`, which itself goes through
P17-DOF's `indicesOf`. There is no `3 * node` and no `dof % 3` anywhere in the
file.

### What is NOT in the file

```text
nodal stress / smoothing / averaging / extrapolation      0 occurrences
a deformed node position, or any write to a mesh          0; every input is
                                                          a const reference
a solve, a factorisation or an iteration                  0
a reaction, or any use of fullResidual()                  0
a conversion to MPa, mm, or any display unit              0
a yield strength, a safety factor or an acceptance
  verdict                                                 0
a mesh-quality threshold                                  0
serialization of any recovered field                      0
```

## The unit matrix

```text
field                      C++ type              internal unit    converted here?
----------------------------------------------------------------------------------
nodal displacement         Translation3D         m (SI)           NO
displacement magnitude     Length                m (SI)           NO

normal strain              double                dimensionless    n/a
engineering shear strain   double                dimensionless    n/a
tensor shear strain        double                dimensionless    n/a

normal stress              Stress (= Pressure)   Pa (SI)          NO
shear stress               Stress                Pa (SI)          NO

principal stress           Stress                Pa (SI)          NO
von Mises stress           Stress                Pa (SI)          NO
hydrostatic stress         Stress                Pa (SI)          NO

principal strain           double                dimensionless    n/a
```

**Strain is a plain `double` and that is deliberate.** P17-DATA settled it:
"DIMENSIONLESS, so plain doubles. There is no Quantity for a ratio, and
inventing one here would be the second unit system this milestone must not
create." It is not labelled `m/m`.

**No display conversion happens in the core**, and the compiler enforces it:
`Stress` and `Length` are `Quantity` types with no implicit conversion to
`double`, so a value cannot silently become a number of megapascals or
millimetres. Four compile-failure cases hold that in force
(`von-mises-as-double`, `displacement-magnitude-as-double`,
`principal-strain-as-stress`, and the two tensor-type cases). `P17-VIZ-001` and
the CLI own presentation.

## Tensor mapping

```text
Stress6 -> StressTensor3        a relabelling. NO factor anywhere.
Strain6 -> StrainTensor3        each gamma HALVED.
```

```text
          [ xx   xy   zx ]                 [ xx      gxy/2   gzx/2 ]
  S   =   [ xy   yy   yz ]        E   =    [ gxy/2   yy      gyz/2 ]
          [ zx   yz   zz ]                 [ gzx/2   gyz/2   zz    ]
```

Both are reached through one accessor, `at(row, column)`:

```text
Voigt 3  XY  ->  at(0,1)  at(1,0)
Voigt 4  YZ  ->  at(1,2)  at(2,1)
Voigt 5  ZX  ->  at(2,0)  at(0,2)
```

**The sixth component is the one that gets misplaced**, and it is numerically
invisible when it does: `zx` and `xz` are the same number, so a transposed
mapping changes nothing until a reader indexes the tensor by hand. That is why
there is one `at` and why `StructuralPost_MapsVoigtStressOntoTheSymmetricTensorPositions`
uses six distinct values and asserts `at(0,2) != 5.0` explicitly.

**Symmetry is structural, not asserted.** There is one `xy` field, so an
asymmetric stress tensor is unrepresentable; the symmetry test is a test of the
accessor over all nine positions rather than of the data.

**The strain tensor's fields are named `xy`, `yz`, `zx` and NOT `gammaXy`.** A
`StrainTensor3` holds tensor shear and a `Strain6` holds engineering shear, and
the two names must not look alike. Passing one where the other is wanted does
not compile (`compile_fail.structpost.strain6-as-strain-tensor`).

## Ordering and cardinality

```text
nodal results      one per current node, ascending by NodeId, no duplicates
element results    one per current Tet4, ascending by ElementId, no duplicates
```

Taken from the mesh's own enumeration, which P16 guarantees is ascending and
uses no unordered container. Asserted on real output rather than assumed:
`is_sorted`, `adjacent_find` and an index-by-index comparison against
`mesh.nodes()` and `mesh.tetrahedra()`.

Each entry **carries its own handle**, which `StructuralResult`'s dense channel
does not. That makes "every current node exactly once, ascending" a property of
the vector rather than an assumption about how it was built, at four bytes per
entry.

## Ownership

```text
RecoveredFields     a STAGE PRODUCT (ADR-040). Carries the AssemblySource of
                    the system it recovered from -- copied forward, never
                    derived -- and answers no currentness question of its own
StructuralResult    P17-DATA's publication type. NOT constructed here: it
                    needs a reaction channel this milestone may not compute
                    and an analysis identity recovery cannot see
derived, never persisted
                    no recovered field is serialized. ADR-030's rule for a
                    mesh, applied to a result by P17-DATA
immutable           every accessor is const and returns a value or a view.
                    There is no setter and no non-const path to the source
                    stamp, so a stale result cannot be made to look current
```

## The optional fields, decided

```text
Hydrostatic stress:   IMPLEMENTED
                      sigma_h = tr(S)/3, in Pa, NORMAL-STRESS sign convention
                      (compression negative). Named `hydrostaticStress` and not
                      `pressure` for that reason; a mean pressure is -sigma_h,
                      is a different quantity, and is deliberately not defined
                      here so the two cannot be conflated.
                      Verified: mean of the normal components, shear ignored,
                      compression negative, and equal to the mean of the three
                      principal stresses on a full 3D state.

Principal strain:     IMPLEMENTED
                      from StrainTensor3, so the gamma/2 is already applied.
                      Descending, dimensionless.
                      Verified: a pure engineering shear gives
                      +|gamma|/2, 0, -|gamma|/2 -- which is the sharpest
                      available measurement of the halving -- plus a diagonal
                      case and the volumetric-strain trace identity.
```

Both were optional. Both were implemented for **verification power** rather
than completeness, and ADR-040 records the argument: the trace identity
`sigma_h == (sigma1+sigma2+sigma3)/3` is a cross-check between the hydrostatic
formula and the eigensolver that neither could provide alone, and the
pure-shear principal strain is the one case that measures the `gamma/2`
directly.
