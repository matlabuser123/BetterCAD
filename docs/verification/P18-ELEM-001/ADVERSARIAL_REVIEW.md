# P18-ELEM-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the Tet4 thermal element before any matrix
          is assembled from it
QUESTIONS: 37 from the brief (section 144), plus 7 of the reviewer's
MUTATIONS: 33 distinct, 33 KILLED, 0 survived, 0 void at the end -- 39 runs: batch 1
           ran 33 (27 killed, 5 void under -Werror, 1 survived), batch 2 reran
           the six (6 killed)
FINDINGS:  6 (one production defect, found by the milestone's own tests)
PRODUCTION DEFECTS: 1 (the degeneracy rule inherited from P16/P17; found by
                    this milestone's own tests, fixed before the freeze)
GATE-BLOCKING: 0
```

Each answer below was obtained by running something or by reading the code.

## Findings

**F1 — the exact-zero degeneracy rule inherited from P16 and P17 accepts
zero-volume tetrahedra (PRODUCTION DEFECT; fixed before the freeze).** The
first run of `CoincidentNodeTetRejected` built the brief's general tetrahedron
with two corners at the same point and the kernel accepted it for corners
(2, 3) and (2, 4): with `e1 == e2`, `e1 . fl(e1 x e3)` is rounding noise, not
zero, and its sign made the element "valid" or "inverted" at random. Measured
over 2000 exactly-degenerate tetrahedra from non-integer coordinates, the
exact-zero rule would have accepted **885** (44 %), each with finite matrices
whose entries were gradients of order 1/noise. Root cause: exact zero is
correct only where the arithmetic is exact (integer fixtures, P17's tests).
General fix: a degree-three-homogeneous bound `16 eps |e1||e2||e3|` derived
from the rounding analysis in FORMULATION.md (analytic 4 eps; measured worst
1.01 eps), plus P16's own `det/6 == 0` underflow test. The same gap exists in
P17's `Tet4Element.cpp`; it is **recorded for the owner and not changed**,
because changing it would require re-qualifying P17-ELEM-001,
P17-ASSEMBLY-001, P17-POST-001 and P17-REFMOD-001 (brief §50, §148). Regression
tests: `CoincidentNodeTetRejected`, `RoundingNoiseIsBelowTheDegeneracyBound`,
`NearDegeneratePolicy`; mutations E10b and E11b.

**F2 — mutation E32 survived the first batch (test strength; fixed).** The
`rho cp V` finiteness gate was shadowed by the per-entry gate behind it: same
error code, same words "not finite", different message. The gate is therefore
redundant for acceptance and valuable for diagnosis, so the diagnostic is now
required to name the product and its units (`CapacityNonFiniteProduct`), and
E32b is killed. Nothing in production changed.

**F3 — five probes did not compile (method; reformulated).** E10, E11, E13,
E22 and E23 left a function or variable unused, which `-Werror` refuses: a
VOID probe, not a survivor. Each was reformulated so the mutated code still
uses every name (E10b, E11b, E13b, E22b, E23b) and rerun against the same
suite; all five were killed.

**F4 — three of the first-run failures were absolute tolerances on quantities
whose error is bounded by the size of the summed products (test method;
restated, not loosened).** The affine gradient, the energy identity and the
net nodal flow of the 1000:1 sliver under a 305.7 K offset showed relative
errors up to 5e-8 against assertions of 1e-11 and 1e-12. The arithmetic can
resolve only `eps` times the products it sums; the assertions are now made in
those units with a bound of 64, and the measured values are 1.63, 0.28 and
0.29. On well-shaped elements the errors are two orders smaller.

**F5 — the kernel refused one of the milestone's own fixtures (test bug; fixed).**
The sparse-handle mesh in `CornersFromMeshInElementOrder` was first written
with an inverted node order; `tet4GeometryProblem` reported
`InvertedElement`, which is the behaviour the milestone exists to provide.
The fixture now takes its positions from the positively oriented general
tetrahedron in the order `[9001, 7, 500000, 104]`.

**F6 — a rho/cp swap cannot be seen by value (limitation; closed by type).**
`Ce` depends on the product, so swapping the two gives bit-identical matrices
(the probe was not written: it would be an equivalent mutant). The protection
is that `ThermalCapacity` holds two distinct `Quantity` types and the swap
is a compile error (`rho-and-cp-are-not-interchangeable`).

**Equivalent mutants considered and not run:** storing `Ke` or `Ce` row-major
versus column-major (both matrices are symmetric); taking `abs` of a volume
already known positive. Recorded so a reader does not count them as gaps.

## The brief's attacks

| Attack | Expected protection | Observed | Test / mutation | PASS |
| --- | --- | --- | --- | --- |
| Can P18-ELEM use mm coordinates instead of m? | `Point3D` is `Length` in SI; no factor in the source | millimetre-literal and metre-literal elements give the same `Ke`, `Ce` to 1e-14; grep for `1e3`/`1000`/`1e-3`/`0.001`: see DEGENERACY.md | `DimensionalAnalysis`; E06 | ✓ |
| Can it sort Tet nodes internally? | no `sort`/`swap` in the kernel; corner order is the element's | grep clean; a sorting mutant moves `G` | `EvenPermutationCovariant`, `GeneralTetGeometry`; E04 | ✓ |
| Can it use abs(detJ) and accept inverted Tets? | `std::abs` only in the magnitude comparison to B; the sign test reads the signed value | an `abs` mutant accepts inverted tets and is killed | `InvertedTetRejected`, `OddPermutationRejected`; E01, E08 | ✓ |
| Can it silently swap inverted nodes? | no reorder code | refused with "not reordered here" in the diagnostic; swapping back restores validity | `InvertedTetRejected` | ✓ |
| Can a zero-volume Tet produce a finite matrix? | the degeneracy gate | **YES under the inherited exact-zero rule (885 of 2000 fixtures)**; NO with the noise bound | `RoundingNoiseIsBelowTheDegeneracyBound`, `CoincidentNodeTetRejected`; E10 | ✓ after the fix |
| Can a near-degenerate criterion be an arbitrary absolute constant? | a relative, degree-3 bound derived from rounding | scale-invariant verdict at 1e-3 .. 1e2 m | `NearDegeneratePolicy`; E11 | ✓ |
| Can J^-1 be used where J^-T is required? | gradient mapping `J^-T` | wrong-transpose mutant fails the general-tet oracle | `GeneralTetGeometry`, `ShapeGradientCoordinateReproduction`; E02, E05 | ✓ |
| Can G rows/columns be transposed? | `g(row=axis, column=node)` in one place | 3 x 4 only; `g(3,0)` and `g(0,4)` return zero; oracle compares the 3 x 4 matrix | `UnitTetGeometry`, `GeneralTetGeometry` | ✓ |
| Can Y/Z gradient components be swapped? | coordinate-reproduction tests | `T = y` must give `(0,1,0)` | `ShapeGradientCoordinateReproduction`; E03 | ✓ |
| Can a constant temperature produce nonzero conduction residual? | null mode | `|Ke 1| / |Ke| = 2.76e-16` | `ConductionConstantTemperatureNull`, `ConstantFieldHasZeroGradient`; E16, E18 | ✓ |
| Can Ke row sums be nonzero? | `sum_j grad Nj = 0` | worst `2.74e-16` | `ConductionRowSumsZero` | ✓ |
| Can Ke be artificially stabilised on the diagonal? | none in source | a mutant adding `1e-6 k V` to the diagonal breaks the null mode and is killed | E16 | ✓ |
| Can Ke be symmetrised after an asymmetric bug? | no `0.5 (K + K^T)` in source | symmetry is exact by construction (`max|Kij - Kji| = 0`); a symmetric-but-wrong averaged mutant is killed by the oracle | `ConductionSymmetric`; E17 | ✓ |
| Can PSD be mistaken for positive definite? | tests assert one zero eigenvalue and rank 3 | `lambda_0 / lambda_3 = 1.79e-16`; rank 3, nullity 1 | `ConductionPositiveSemidefinite`, `ConductionRankThree` | ✓ |
| Can the constant-temperature null mode be removed? | tested directly | a single scaled diagonal entry breaks it and is killed | E18 | ✓ |
| Can conductivity scaling be nonlinear? | linear in `k` | `Ke(c k) = c Ke(k)` to 1e-15 | `ConductivityScaling` | ✓ |
| Can geometry scaling give Ke ∝ s^3 instead of s? | scale-law test | `Ke` ratio equals `s` at five scales | `GeometryScalingConduction`; E07, E14, E15 | ✓ |
| Can Ce scale as s rather than s^3? | scale-law test | `Ce` ratio equals `s^3` | `GeometryScalingCapacity` | ✓ |
| Can Ce use conductivity by mistake? | no `k` in `computeTet4Capacity`'s signature | `Ce.total() = rho cp V`, not `k V`; passing `k` is a compile error | `CapacityNeedsNoConductivity`; compile-fail `capacity-takes-no-conductivity` | ✓ |
| Can Ke use rho/cp by mistake? | no `rho`/`cp` in `computeTet4Conduction` | distinct values; `Ke(rho)` differs from `Ke(k)` by > 50 % | `FieldSwapProtection`; compile-fail `a-density-is-not-a-conductivity` | ✓ |
| Can rho and cp be swapped in API plumbing? | two distinct `Quantity` types | the swap gives the same product at runtime, so value tests cannot see it; the **compile** fails | compile-fail `rho-and-cp-are-not-interchangeable` | ✓ |
| Can a steady element require rho/cp? | `Ke` takes geometry and `k` only | a k-only material resolves for steady, `capacity()` empty, `Ke` formed | `SteadyMaterialIntegration`, `ConductionNeedsNoCapacity` | ✓ |
| Can transient Ce be built when rho/cp are missing? | the pair is required; a k-only transient resolution is `InputsUnavailable` (P18-MAT) | no `Ce` without a `ThermalCapacity` | `TransientMaterialIntegration` | ✓ |
| Can the consistent matrix accidentally become diagonal lumped? | full-matrix tests | off-diagonals all positive; `Ce_ii = 2 Ce_ij`; rank 4 | `CapacityConsistentPolicy`, `OnePointQuadratureIsNotTheConsistentMatrix`; E22, E24 | ✓ |
| Can one-point centroid quadrature be used for N^T N? | full-matrix + rank tests | centroid matrix has the same row sums and total, rank 1; production differs in every entry | `OnePointQuadratureIsNotTheConsistentMatrix`; E23 | ✓ |
| Can capacity row-sum tests pass while the full Ce is wrong? | yes, if only row sums were tested | row sums are tested **and** the full matrix, the ratio, the spectrum and the rank | `CapacityRowSums` + the above; E23, E25, E26, E33 | ✓ |
| Can Ce become rank 1 from centroid quadrature? | rank/spectrum test | rank 4; eigenvalues `1.49e5 x3, 7.47e5` | `CapacityFullRank`; E23 | ✓ |
| Can total heat capacity fail rho cp V? | conservation test | worst relative error `1.65e-16` | `CapacityTotalHeatCapacity`; E27-E29 | ✓ |
| Can translation alter G/Ke/Ce? | edge vectors only | changes of 1e-16 relative; 1e6 m offset measured at 8e-11 | `TranslationInvariant`, `HugeOffsetTranslation` | ✓ |
| Can proper rotation alter Ke for isotropic k? | `G' = R G` | `Ke` change 1.96e-16 | `RotationInvariant`, `GradientRotatesWithGeometry` | ✓ |
| Can a reflected/inverted Tet be mislabeled rotation invariant? | `det R = +1` asserted before use | the mirror is refused as inverted | `ReflectionIsNotARotation` | ✓ |
| Can node permutation compare raw matrices without permutation transform? | `P` built from `sigma` in the test | covariance `1.96e-16`; the raw matrices differ by 0.6 | `EvenPermutationCovariant`; E09 | ✓ |
| Can local thermal ELEM depend on global NodeId/DofIndex? | the kernel includes neither | corners are coordinates; a `NodeId` or `ThermalDofIndex` array is a compile error; only `cornersOf` meets a mesh | compile-fail `corners-are-not-node-ids`, `corners-are-not-dof-indices`; `CornersFromMeshInElementOrder` | ✓ |
| Can a poor-quality but valid Tet be rejected here instead of P18-VALID? | no quality rule | the 1000:1 sliver is computed | `NearDegeneratePolicy` | ✓ |
| Can production formula be reused as the test oracle? | the reference shares no code | see ANALYTICAL_ORACLES.md | audit | ✓ |
| Can nonfinite matrix entries escape? | finiteness gates on gradients, `Ke`, `rho cp V`, `Ce` | each gate has a test that fails without it | `NonFiniteResultRejected`, `CapacityNonFiniteProduct`; E13, E21, E32 | ✓ |
| Can zero tests execute and still report PASS? | `ZeroMatchGuard` | 59 >= 40; the seven group filters and `compile_fail.thermalelem.` 17 >= 8 | the guard | ✓ |

## The reviewer's own seven

**1. Is the new degeneracy bound just a loosened threshold in disguise?** No,
and the direction matters: it makes the kernel stricter, not looser. The
inherited exact-zero rule accepted 885 of 2000 exactly-degenerate
tetrahedra; the bound rejects all 2000. It rejects nothing a quality-gated mesh
can contain (the sliver at 2e-3 sits twelve orders above it). It was derived
before the factor was chosen, and measured afterwards (1.01 eps observed,
4 eps analytic, 16 eps used).

**2. Why compare against `|e1||e2||e3|` rather than a volume or a longest
edge?** Because that product is the exact scale of the arithmetic: the rounding
error of the triple product is proportional to it. A bound on a longest edge
cubed would be loose for an element with one short edge, and a bound on the
volume is circular.

**3. Does the bound make orientation a function of scale or of the node
order?** It does not. It depends on the edge lengths from corner 1, so it is
node-order dependent in the sense `det J` is; but a tetrahedron far above the
bound is classified identically for every ordering, and one at the bound is
degenerate for every ordering because the true volume is zero.

**4. Could the capacity matrix be unit-correct and the policy still wrong?**
Yes, which is why the policy decision is written down with its trade-off:
consistent capacity can give a non-monotone first backward-Euler step at small
time steps. That is a property of the time integration, assigned to
P18-TRANSIENT-001 to measure, and not hidden.

**5. Is `Tet4Geometry::gradientFrom` a back door into post-processing?** It
returns a `TemperatureGradient3` only and computes `G Te`, the shared path
P18-POST-001 will call; it forms no flux, takes no conductivity and publishes
nothing. Flux recovery is not implemented here.

**6. Does `cornersOf` weaken the "no global identity" claim?** It is the one
function that meets a mesh, by identity through `Mesh::findNode`, never by
indexing with a handle's value (the P16 rule P18-DOF respected). It returns
coordinates in the element's own order and fails `NotFound` for a node the
mesh does not hold; the kernel proper still consumes coordinates.

**7. Is `Ke`'s exact symmetry (`max|Kij - Kji| = 0`) a property of the
formulation or of this compiler?** Of the formulation: `Ke_ij` and `Ke_ji`
evaluate the same three products in the same order, with the factors
commuting term by term, so any IEEE-754 compiler without fused contraction
reassociation produces identical values. The test asserts exact equality and
would fail loudly, rather than drift, if a toolchain broke that.

## Mutation probes

Each probe applies one textual change to `ThermalElement.cpp`, rebuilds the three
test-facing targets, and runs every `ThermalElement_*` case in its own process through
ctest, so a mutant that aborts is still counted case by case. The file is
restored after each probe and was grepped for every mutant fragment (none left) and
for every original line (all present) after each batch. Scripts and summaries are
under [qualification/](qualification/). The suite had 59 cases for batch 1 and 60 for
batch 2 (the bit-fingerprint case and one diagnostic check were added between the
two, as pure additions of checks, so every batch-1 kill stands).

| # | Mutation | Verdict | Cases | First killing cases |
| --- | --- | --- | --- | --- |
| E01 | det J returned as abs(det J) (an abs repair) | KILLED | 5 | InvertedTetRejected, NearDegeneratePolicy, OddPermutationRejected, ReflectionIsNotARotation |
| E02 | gradients from J^-1, not J^-T | KILLED | 11 | AffineTemperatureGradient, CapacityPhysicalMaterial, ConductionEnergyIdentity, EvenPermutationCovariant |
| E03 | y and z gradient components swapped | KILLED | 6 | AffineTemperatureGradient, GeneralTetGeometry, GradientRotatesWithGeometry, RotationInvariant |
| E04 | corners sorted by x before the Jacobian | KILLED | 22 | AffineTemperatureGradient, CapacityConsistentPolicy, CapacityFullRank, CapacityPhysicalMaterial |
| E05 | J stored transposed, mapping unchanged | KILLED | 13 | AffineTemperatureGradient, CapacityPhysicalMaterial, ConductionEnergyIdentity, EvenPermutationCovariant |
| E06 | a x1000 mm/m factor in the Jacobian | KILLED | 26 | AffineTemperatureGradient, CapacityConsistentPolicy, CapacityFullRank, CapacityPhysicalMaterial |
| E07 | V = det J (no /6) | KILLED | 18 | CapacityConsistentPolicy, CapacityFullRank, CapacityPhysicalMaterial, CapacityPositiveDefinite |
| E08 | the det < 0 refusal removed | KILLED | 5 | InvertedTetRejected, NearDegeneratePolicy, OddPermutationRejected, ReflectionIsNotARotation |
| E09 | a valid even permutation refused | KILLED | 21 | AffineTemperatureGradient, CapacityConsistentPolicy, CapacityFullRank, CapacityPositiveDefinite |
| E10b | noise bound zeroed (the inherited exact-zero rule) | KILLED | 3 | CoincidentNodeTetRejected, NearDegeneratePolicy, RoundingNoiseIsBelowTheDegeneracyBound |
| E11b | noise bound replaced by an absolute 1e-12 | KILLED | 2 | NearDegeneratePolicy, NonFiniteResultRejected |
| E12 | P16's det/6 == 0 test removed | KILLED | 1 | NonFiniteResultRejected |
| E13b | gradient finiteness gate removed | KILLED | 1 | NonFiniteResultRejected |
| E14 | Ke without the V factor | KILLED | 9 | CapacityPhysicalMaterial, ConductionEnergyIdentity, ConductionPositiveSemidefinite, GeneralTetConductionOracle |
| E15 | Ke with det J (6V) in place of V | KILLED | 7 | CapacityPhysicalMaterial, ConductionEnergyIdentity, ConductionPositiveSemidefinite, GeneralTetConductionOracle |
| E16 | 1e-6 k V added to the Ke diagonal | KILLED | 13 | CapacityPhysicalMaterial, ConductionConstantTemperatureNull, ConductionEnergyIdentity, ConductionPositiveSemidefinite |
| E17 | Ke averaged with an xy-only dot product | KILLED | 8 | CapacityPhysicalMaterial, ConductionEnergyIdentity, ConductionPositiveSemidefinite, GeneralTetConductionOracle |
| E18 | Ke(0,0) scaled by 1.01 (null mode broken) | KILLED | 13 | CapacityPhysicalMaterial, ConductionConstantTemperatureNull, ConductionEnergyIdentity, ConductionPositiveSemidefinite |
| E19 | k > 0 check removed (finite only) | KILLED | 3 | CapacityRejectsInvalidDensity, CapacityRejectsInvalidSpecificHeat, ConductionRejectsInvalidConductivity |
| E20 | k finiteness check removed (> 0 only) | KILLED | 3 | CapacityRejectsInvalidDensity, CapacityRejectsInvalidSpecificHeat, ConductionRejectsInvalidConductivity |
| E21 | Ke entry finiteness gate removed | KILLED | 1 | NonFiniteResultRejected |
| E22b | Ce lumped: rho cp V / 4 on the diagonal, 0 off it | KILLED | 13 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityPhysicalMaterial |
| E23b | Ce by one-point centroid rule: rho cp V / 16 everywhere | KILLED | 8 | CapacityConsistentPolicy, CapacityFullRank, CapacityPhysicalMaterial, CapacityPositiveDefinite |
| E24 | Ce off-diagonal = diagonal | KILLED | 12 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityPhysicalMaterial |
| E25 | Ce diagonal = off-diagonal / 2 | KILLED | 12 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityPhysicalMaterial |
| E26 | Ce off-diagonal rho cp V / 10 | KILLED | 12 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityPhysicalMaterial |
| E27 | rho omitted from Ce | KILLED | 14 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityNonFiniteProduct |
| E28 | cp omitted from Ce | KILLED | 15 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityNonFiniteProduct |
| E29 | Ce uses (rho + cp) V | KILLED | 15 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityNonFiniteProduct |
| E30 | abs(rho) accepted | KILLED | 2 | CapacityRejectsInvalidDensity, CapacityRejectsInvalidSpecificHeat |
| E31 | abs(cp) accepted | KILLED | 1 | CapacityRejectsInvalidSpecificHeat |
| E32b | rho cp V finiteness gate removed (survived once) | KILLED | 1 | CapacityNonFiniteProduct |
| E33 | Ce scaled by 6 (det J instead of V) | KILLED | 12 | CapacityConsistentPolicy, CapacityFullRank, CapacityNeedsNoConductivity, CapacityPhysicalMaterial |


## Verdict

PASS. 37 attacks from the brief and 7 of the reviewer's, each answered by a
measurement, a mutation or a compile-time refusal; 33 distinct mutation
probes, 33 killed, 0 survived, 0 void at the end (one survivor and five void
probes en route, all closed); 6 findings, one of them a production defect --
the inherited exact-zero degeneracy rule, which accepted 885 of 2000
exactly-degenerate tetrahedra -- found by this milestone's own test, fixed
before the freeze, and recorded for the owner as a latent gap in P17's
kernel that this milestone did not change; 0 gate-blocking. The kernel that
was qualified is the one the probes were applied to and restored from, line
for line, and its results are bitwise identical across the three presets.
