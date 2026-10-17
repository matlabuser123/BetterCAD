# P18-ELEM-001 — orientation, degeneracy, and the source audits

```text
TESTS:  ThermalElement_InvertedTetRejected, _CoplanarTetRejected,
        _CoincidentNodeTetRejected, _NearDegeneratePolicy,
        _RoundingNoiseIsBelowTheDegeneracyBound, _NonFiniteCoordinateRejected,
        _NonFiniteResultRejected, _ReflectionIsNotARotation, _OddPermutationRejected
```

## The policy, as implemented

```text
coordinate not finite          NonFiniteCoordinate    InvalidArgument        before any arithmetic
det J not finite               DegenerateElement      FailedPrecondition     P16's classification
|det J| <= B                   DegenerateElement      FailedPrecondition     rounding noise
det J / 6 == 0                 DegenerateElement      FailedPrecondition     P16's own volume test
det J < -B                     InvertedElement        FailedPrecondition     REFUSED, never reordered
det J >  B                     valid
gradient / entry not finite    NonFiniteResult        FailedPrecondition     result gate

B = 16 eps |e1||e2||e3|        eps = 2^-52 = 2.22e-16;  e_i = corner_{i+1} - corner_1
```

The diagnostics are enum values with an `optional<ElementProblem>` query
(`tet4GeometryProblem`, `conductivityProblem`, `capacityProblem`), so no
test matches on free text for classification; the message substrings that are
asserted are additional.

## Where this departs from P16 and P17, and the evidence that forced it

P16 (`MeshValidation`) and P17 (`TET4_DERIVATION.md`) call a tetrahedron
degenerate when its volume is **exactly** zero. The brief asked for a
scale-aware criterion derived from geometry and floating precision "if a new
threshold is required". It was required, and the first run of this
milestone's own tests showed it:

```text
CoincidentNodeTetRejected, first run: corners (1,2) and (1,3) coincident on the
brief's general fixture -> the exact-zero rule returned ACCEPTED.
```

With `e1 == e2` the product `e1 . fl(e1 x e3)` is not zero in floating point;
it is rounding noise of either sign. The measurement in
`RoundingNoiseIsBelowTheDegeneracyBound` makes the scale of the gap explicit:

```text
2000 exactly-degenerate tetrahedra from non-integer coordinates:
    400 coplanar (4th corner an affine combination of the other three)
    1600 with two corners coincident (4 pairings x 400)
the exact-zero rule would have accepted                 885 of 2000  (44 %)
worst |det J| / (eps |e1||e2||e3|) produced by noise    1.01
kernel verdict                                          degenerate for all 2000
```

885 zero-volume tetrahedra would have produced finite matrices whose entries
were gradients of order `1/noise`: "Can a zero-volume Tet produce a finite
matrix?" answered YES by the predecessor rule, NO by this one.

**The bound is derived, not tuned.** The rounding analysis is in
FORMULATION.md: the computed `det J` differs from the true value by at most
about `4 eps |e1||e2||e3|`; the measured worst noise is `1.01 eps ...`; the
factor 16 is four times the analytical bound and sixteen times the measured
worst case. It is homogeneous of degree three in the coordinates, as `det J`
is, so it is **not an absolute constant**: `NearDegeneratePolicy` verifies that
a valid, an inverted and a coplanar tetrahedron are classified identically at
scales of 1e-3, 0.1, 1 and 100 m, and that heights of 1e-12 and 1e-17 of the
scale straddle the bound at every scale (mutation E11 replaces it by an absolute
`1e-12` and is killed there).

**It is not a quality rule.** `B / (|e1||e2||e3|) = 3.553e-15`. The 1000:1
sliver has a ratio of about 2e-3; it is **accepted** and computed
(`V = 1.667e-04 m^3`, `max|Ke| = 7.125e+03 W/K`, finite, null mode and row
sums intact). P17-VALID-001 rejects slivers on measured accuracy laws long
before this bound (ADR-042); the thermal analogue is P18-VALID-001's. Above the
bound the classification is exactly P16's.

**P17 is not changed.** The same latent gap exists in `Tet4Element.cpp`; it
is recorded here as a finding for the owner, not repaired, because touching
P17 would require re-qualifying P17-ELEM-001, P17-ASSEMBLY-001, P17-POST-001
and P17-REFMOD-001 (brief §50, §148).

## The cases

| Case | Input | Result |
| --- | --- | --- |
| inverted | general fixture, corners 1 and 2 swapped | `InvertedElement`; chain overloads fail; swapping back restores validity |
| coplanar | `(0,0,0) (1,0,0) (0,1,0) (1,1,0)`; also three collinear + one off | `DegenerateElement` |
| coincident | every pair of the 4 corners equal on the general fixture | `DegenerateElement` (all six pairs) |
| near-degenerate | straddle B at 1e-12 / 1e-17 of the scale, three scales | valid / `DegenerateElement`; exact-zero also degenerate |
| 1000:1 sliver | positive volume | **accepted**, matrices finite |
| non-finite coordinate | NaN, +inf, -inf at each of four corners | `NonFiniteCoordinate`, `InvalidArgument` |
| volume underflow | `(0,0,h)`, `h = denorm_min` | `DegenerateElement`; `signedVolume == 0` agrees |
| result overflow | `(0,0,h)`, `h = 1e-310` | passes geometry, **`NonFiniteResult`** from the gradient gate |
| triple product overflow | edges 1e200 | `DegenerateElement` (P16: non-finite determinant) |
| reflected | `det R = -1` | `InvertedElement`, not "rotation-invariant" |

## Source audits

Run on the final tree, after the last source or test edit and before the freeze.
The questions:

```text
abs(det) / fabs      every occurrence classified
swap / sort / reorder inside the kernel
millimetre / thousand factors   1e3  1000  1e-3  0.001  *1000  /1000
static / mutable / thread_local / global state
Eigen / linear algebra included by production
symmetrisation       0.5 *, transpose averaging
regularisation       epsilon added to a diagonal
global identity      NodeId / ThermalDofIndex / ThermalFreeEquationMap in the kernel
test-side            thermal:: symbols inside the independent reference
```

```text
AUDIT                                                       CODE HITS  RESULT
std::abs / fabs / abs(                                      1          ThermalElement.cpp:248 -- the MAGNITUDE
                                                                        comparison |det J| > B. The sign test that
                                                                        follows (`det < 0.0`) reads the SIGNED value.
                                                                        The other hit is a comment ("no std::abs").
sort / swap / reverse / rotate / next_permutation / ranges  0          none, in code or comments
unit conversion: 1e3 1000 1e-3 0.001 mm milli kilo          0          two COMMENT hits, both the phrase "1000:1 sliver"
                                                                        / "relative volume about 1e-3"; no factor in code
static / mutable / thread_local / atomic / mutex            0          seven COMMENT hits (the words "no static state",
                                                                        "no global", "nothing mutable"); none in code
Eigen / linear-algebra include                              0          three COMMENT hits (the sentence that says there
                                                                        is none); the header's includes are core, meshing,
                                                                        thermal and the standard library only
0.5 * ( / transpose / symmetrise                            0          four COMMENT hits stating the rule
epsilon / regularise / stabilise                            1          ThermalElement.cpp:152 -- numeric_limits::epsilon()
                                                                        in the degeneracy bound, a floating-point precision
                                                                        constant; two comment hits
NodeId / ThermalDofIndex / FreeEquationMap / ConstraintMap  0          the 20 hits are the SIZE constant kTet4ThermalDofs
                                                                        (= 4) and comments; no global identity type is
                                                                        included or named in code. cornersOf is the one
                                                                        function that meets a mesh, by Mesh::findNode.
independent reference (tests): 'thermal::' / 'meshing::'    0 / 0      the 44-line reference() region touches production
                                                                        only through Point3D coordinate reads
```

The kernel's includes are `core/Error`, `core/Units`, `core/math/Point`,
`core/units/{Dimension,Quantity}`, `meshing/Mesh`, `thermal/Export`,
`thermal/ThermalData`, `thermal/ThermalMaterial` and the standard library.
It includes nothing from `structural/` (ADR-050 forbids it), nothing from
`ThermalDof.hpp` and nothing from a later milestone; the layering checker
reports 509 files and 0 violations on the tree that holds it.
