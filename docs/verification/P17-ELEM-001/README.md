# P17-ELEM-001 — Tet4 Linear Elastic Element

```text
STATUS:   PASS
MILESTONE: P17-ELEM-001, the fifth milestone of P17 — Structural FEA
SCOPE:    the local element kernel: shape gradients, B, D and Ke. No global
          assembly, no loads, no restraints, no solver, no stress recovery.
```

## Baseline

```text
HEAD at start      28b0d5aa7215ee6c16100bccdb9504a220af5500
origin/main        28b0d5aa7215ee6c16100bccdb9504a220af5500
working tree       clean, 0 porcelain lines
P17-ARCH-001  PASS 20/20     P17-DATA-001  PASS 19/19
P17-MAT-001   PASS 14/14     P17-DOF-001   PASS 12/12
P16           QUALIFIED
and the eight paths still fingerprinted P17-DOF-001's qualified component
list, so every predecessor was demonstrably qualified on THIS tree
```

## The specification was written first

[TET4_DERIVATION.md](TET4_DERIVATION.md) fixes the reference element, the shape
functions, the Jacobian convention, the physical-gradient transformation, the
signed-volume formula, the `B` convention, the `D` convention, the local DOF
ordering and the `Ke` formula. It was written **before** the implementation and
the tests check the code against it, not the other way round.

## What the audit decided before any code was written

**P16's degeneracy criterion has no tolerance, and that is deliberate.**
`MeshValidation.hpp` says so in as many words — "there is no tolerance in this
file … A thin tetrahedron is data-valid and is P16-QUALITY-001's to complain
about" — and the predicate, read from the source, is

```text
!isFinite(V) -> degenerate    V == 0 -> degenerate    V < 0 -> inverted
```

So the brief's instruction to "audit P16's scale-aware degeneracy policy" has a
different answer than it anticipates: **there is no scale-aware policy, because
there is none at all.** This kernel applies the same rule, in the same order,
with the same classification. A 1000:1 thin tetrahedron is **accepted**, with a
test asserting it — refusing it would make a qualified mesh unsolvable, and
`invalid` is not `poor quality`.

**`mu` already exists.** `LinearElasticConstants::shearModulus` is
`E / (2(1+nu))`, derived by P15 with exactly the formula the brief fixes, so
production carries P15's value rather than recomputing it — the fourth
milestone running in which the audit found the quantity already built. `lambda`
is not provided and comes from the frozen formula; the identity
`lambda = K - 2 mu / 3` cross-checks it against P15's *other* derived constant.

**No Eigen in production**, for three reasons and the third is decisive: the
public API must carry `Ke`, `B` and `D` to `P17-ASSEMBLY-001` and
`P17-POST-001`, and **no public header in this repository includes Eigen** — it
is a `PRIVATE_LINK` of both `sketch` and `assembly` — so these types could not
have been Eigen types anyway. What the kernel needs is a 3x3 determinant and
inverse and two fixed products: arithmetic, not a library. And
`src/structural/CMakeLists.txt` records that admitting Eigen here is
`P17-SOLVE-001`'s decision, carrying a licence question the owner must answer.
The **tests** use Eigen freely, which is also what lets the independent
reference take a different route.

## What this milestone adds

```text
computeTet4Kinematics(nodes)      volume, grad Ni, B (6x12, 1/m)
isotropicElasticity(constants)    D (6x6, Pa)
computeTet4Stiffness(kin, elas)   Ke = V B^T D B (12x12, N/m)
```

Split three ways because the three have different inputs and different
lifetimes: kinematics is geometry alone and survives a material edit, `D` is
material alone so one serves every element of a body, `Ke` needs both. An
assembler over a hundred thousand elements builds `D` once.

**`B` and `D` are public on purpose.** `P17-POST-001` recovers
`eps = B u_e` and `sigma = D eps`, and if `B` were private to the translation
unit that milestone would reimplement it — with its own transpose and its own
shear convention, which is how a stress field ends up wrong in the shear terms
only. `Tet4Kinematics::strainFrom` and `ElasticityMatrix::stressFrom` are the
shared paths.

The dimensional reasoning is **proved by the compiler**, not asserted in a
comment:

```cpp
static_assert(decltype(Volume{} * InverseLength{} * Stress{} *
                       InverseLength{})::dimension
              == dimensions::force / dimensions::length);
```

## The production defect this milestone found in itself

`TET4_DERIVATION.md`'s first draft claimed `det J` was "character-for-character
the expression `meshing::signedVolume` computes". The bit-identity test
**failed**:

```text
CHECK( k.volume().si() == p16.si() )
  0.21253133333333338 == 0.21253133333333332
```

One ulp, on the skew tetrahedron. A first-row cofactor expansion is the same
algebra as `cross` then `dot` and **not** the same floating-point operations.
The claim was asserted from an identity rather than measured.

Fixed by matching P16's association rather than relaxing the test, because the
two functions are predicates on the **same boundary**: a tetrahedron whose
signed volume rounds to within an ulp of zero could be data-valid for the mesh
validator and inverted for the solver — a qualified mesh containing an element
the solver refuses. Practically unreachable; structurally real; removable for
free. Measured afterwards: **exactly zero** disagreement on every element of
RM-MESH-01, RM-MESH-03 and RM-MESH-07.

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) finding F1.

## Tests

```text
tests/structural/Tet4ElementTests.cpp                 25 ctest entries
  the reference gradients and V = 1/6, EXACT -- asserted with ==, not a
    tolerance, because for J = I every entry is a small integer
  sum grad Ni == 0 on four fixtures, scale-aware
  V equals meshing::signedVolume BIT FOR BIT
  an inverted Tet refused and NOT reordered; swapping back restores validity
  coplanar, coincident, collinear, NaN, +Inf, -Inf all refused
  a thin 1000:1 Tet ACCEPTED, with finite gradients and Ke
  a reflection (det R = -1) refused; a node swap restores it
  D symmetric at six ratios including auxetic and nu = 0.499
  lambda == K - 2 mu / 3, through P15's bulk modulus
  mu IS P15's shearModulus, not a recomputation
  nu = 0.5, -1 and NaN refused as invalid_material
  tau = mu gamma, and asserted NOT 2 mu gamma, for all three shears
  uniaxial and volumetric stress against the closed forms
  every affine strain state reproduced exactly: six pure cases and two mixed
  B gives zero strain for all six rigid-body modes
  B's row order == TensorComponent's, with every zero entry checked
  the local DOF order == P17-DOF's global numbering, index by index
  Ke symmetric and finite on five fixtures, residual relative to the norm
  Ke against the hand-built analytical unit Tet, and two entries as rationals
  Ke against the INDEPENDENT reference on 22 fixtures
  six rigid-body modes explicitly, then 6 zero / 6 positive eigenvalues, then
    SVD rank 6
  u^T Ke u >= 0 over 64 deterministic vectors; U from Ke == U from V eps D eps
  virtual work reciprocal; a rigid translation implies no force
  translation invariance at three offsets to 1e6 m
  Ke proportional to s and to E, five scales and four factors
  covariance under a proper rotation, Ke_rot = T Ke T^T, plus four invariants
  the same element in mm and m, GPa and Pa, gives the same Ke
  an even node permutation gives P Ke P^T; three odd ones are refused
  bit-identical over 16 repeats

tests/reference/Tet4ElementReferenceTests.cpp          4 ctest entries
  every element of RM-MESH-01/03/07 accepted, positive, finite, symmetric
  P17's volume == P16's for every element, exactly
  the element volumes SUM to VolumeMesh::tetrahedralVolume() to 1e-12
  the batch fingerprint identical over 4 repeats
```

## Zero-match protection

Counted with `-N` **before** every run:

```text
Tet4Element_      29        architecture.     14
```

The final regression is unfiltered in all three presets.

## Mutation protection

```text
PROBES                      13   (11 distinct defects, 2 re-run)
KILLED BY TESTS             11
SURVIVED, STRUCTURALLY       2   both no-ops, with their reasons measured
KILLED ONLY BY THE COMPILER  0   after the two re-runs
```

Every high-risk FEA formulation error the brief names has a probe: `abs(detJ)`,
`J^-1` for `J^-T`, `2 mu` on the shear diagonal, swapped `gyz`/`gzx` rows,
component-blocked local DOFs, `V` dropped, `abs(V)`, post-symmetrisation,
diagonal regularisation, tensor-shear `B` against engineering `D`, and the
three validity branches removed one at a time.

The most informative kill: adding `1e-3 V` to the diagonal is **not** caught by
the rigid-body test — `1e-3` relative sits inside its scale-aware band — but by
the two **scale laws**. [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Independent reference and analytical values

A different route: a 4x4 coordinate-matrix inverse instead of a 3x3 Jacobian,
so no Jacobian is ever formed and the `J^-1`/`J^-T` defect cannot be mirrored;
`lambda` and `mu` recomputed from `E` and `nu` instead of taken from P15; Eigen
products instead of hand loops. Independence is **enforced** by three
mechanisms rather than promised — see
[INDEPENDENT_REFERENCE.md](INDEPENDENT_REFERENCE.md).

Worst relative error over 22 fixtures: **2.3e-15**, against an asserted bound
of 1e-11. The unit tetrahedron is additionally built a **third** time, by hand,
so that case has three independent descriptions. All measured numbers are in
[NUMERICAL_VALIDATION.md](NUMERICAL_VALIDATION.md).

## Determinism

No unordered container, no `static` storage, no `mutable`, no `thread_local`.
Repeated evaluation is asserted **bit-identical** within a build — `==` on the
whole value, not a tolerance — because the computation is a fixed sequence of
scalar arithmetic over its arguments.

**Cross-preset equivalence**, which matters most here because every claim in
the milestone is a floating-point comparison:

```text
[elem]         debug-ext         All tests passed (4384 assertions in 29 cases)
               release-ext       All tests passed (4384 assertions in 29 cases)
               debug-shared-ext  All tests passed (4384 assertions in 29 cases)

[structural]   all three         All tests passed (31146 assertions in 110 cases)
```

Identical, including the bit-exact volume comparisons, the exact-equality
gradient assertions and the `nu = 0.499` near-incompressible fixture — the
values most likely to disagree if an optimiser reassociated an expression.

## Adversarial review

```text
QUESTIONS                       33  (29 from the brief, 4 of my own)
FINDINGS                         3
PRODUCTION DEFECTS               1  F1, fixed in this milestone
CLAIMS CORRECTED                 1  F1's "character-for-character"
PROBE DESIGN ERRORS OF MINE      1  F3, a kill by -Werror is a useless kill
UNKILLABLE PROBES, RECORDED      2  F2, with reachable probes added instead
GATE-BLOCKING                    0
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 22:10:43 to 01:03:58,
**2 h 53 min 15 s**, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3522/3522
release-ext            0       0      0         0           0   3522/3522
debug-shared-ext       0       0      0         0           0   3522/3522

REPEAT (5x each of 797 selected tests, back to back)
release-ext            0                            797/797   654.53 s
debug-ext              0                            797/797   719.66 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        612 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs
```

**3522** is P17-DOF-001's 3493 plus this milestone's 29 entries; both counts
were taken independently and reconciled rather than one derived from the other.

**612** objects is 609 plus exactly three new translation units, confirmed by
name in the build log: `Tet4Element.cpp`, `Tet4ElementTests.cpp` and
`Tet4ElementReferenceTests.cpp`.

**797** repeated tests is P17-DOF-001's 765, plus this milestone's 29, plus 3
that the new `unit\.Tet` term pulls in from P16's tetrahedron-validation suite.
The filter was checked **from inside the selection** — grepping the
milestone's own tests among the selected 797 and requiring 29 — not by reading
it, which is the check added after P17-DOF-001 found an inherited filter
covering 17 of 31 tests.

## Known limitations

```text
Nothing calls this code yet. There is no global matrix, no load, no restraint
  and no solve, so the kernel's only consumers today are its own tests and the
  reference-mesh pass. P17-LOAD-001 onwards are not authorized.

The shared B and D are OFFERED, not ENFORCED. P17-POST-001 is meant to call
  strainFrom and stressFrom rather than rebuild either, and the header says so
  -- but nothing prevents a later milestone writing its own. Making that
  impossible would need the element to own the recovery, which is that
  milestone's scope.

The admissible material RANGE is not checked here. E <= 0, nu > 0.5 and
  nu < -1 give a finite but indefinite D, and the element would compute a
  finite, indefinite Ke from it. P15 refuses those values at the point of entry
  (P17-MAT-001), so a material resolved through StructuralMaterial cannot be
  out of range; a hand-filled LinearElasticConstants can be, and only
  finiteness is guarded. Re-deriving the range here would be the second
  definition of admissibility ADR-028 forbids.

Translation invariance degrades with distance from the origin, and the
  tolerance is stated per offset rather than uniformly: 1e-14 relative at
  metres, 1e-12 at kilometres, 1e-9 at 1e6 m. That is subtractive cancellation
  in the coordinate differences, measured rather than wished away.

The independent reference is algorithmically distinct but was not derived
  symbolically with an external tool. The reference tetrahedron's values were
  derived by hand and are checkable in a few lines; the skew and random cases
  rest on the algorithmic independence.

Two mutation probes cannot be killed -- abs(V) after the validity gate, and
  post-symmetrising an already-exactly-symmetric matrix. Both are no-ops for
  stated structural reasons, with reachable probes added in their place.

No element-quality acceptance policy. P16-QUALITY-001 measures and
  P17-VALID-001 decides; this kernel only separates invalid from valid.

This MinGW toolchain has no ASan/UBSan. Inherited and recorded.
```

## The qualified tree is the committed tree

```text
| WHEN                               | WHOLE FINGERPRINT                        |
| frozen, before the first configure  | 845b118ce6edaa9286ca5731dc43a04b6842a757 |
| recorded by the harness after the   | 845b118ce6edaa9286ca5731dc43a04b6842a757 |
|   last test of the last preset      |                                          |
| recomputed before the commit        | 845b118ce6edaa9286ca5731dc43a04b6842a757 |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            10bf6abc0c4a5e27ae2dc362a3b0be4a98601e0c
src                f6ddc1dbe2d4edad23cca52a8174a34be0d64d2c
tests              61b2ef80697f91ed62770999fb7d7c6f3b279399
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Exactly three paths moved from P17-DOF-001's, and they are the three this
milestone touches; the other five are byte-identical, which is the check a
milestone that had quietly edited `cmake/` or `CMakePresets.json` would fail.

The whole value is a function of the eight paths **plus the base tree HEAD
pointed at**, which was `28b0d5a` throughout. The invariant that survives a
moving HEAD is the component list, checkable against the published tree in one
command:

```bash
git fetch origin && for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do echo "$p $(git rev-parse "origin/main^{tree}:$p")"; done
```

What moved after the freeze: `docs/verification/P17-ELEM-001/`, `TODO.md`,
`ROADMAP.md` and `README.md` — all documentation, none inside the fingerprint,
none configured, compiled, linked or read by a test.

[FREEZE.md](FREEZE.md) records the four pre-freeze checks, the mutation
harness's verified restoration, and the harness provenance: `qualify.cmd` is
byte-identical at `d313a64070718c44fae290ac042fe259d1a03c8b`, unchanged since
P16-SIZE-001 and now fifteen milestones in a row, hashed against the previous
milestone's copy rather than assumed.

## Result

```text
RESULT:   PASS
TESTS:    3522/3522 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 612 objects; 797 x 5 repeats in two presets;
          17 stages, 0 failed
MUTATION: 13 probes, 11 killed by tests, 2 structural no-ops
TREE:     845b118ce6edaa9286ca5731dc43a04b6842a757, identical at all three
          readings
EVIDENCE: this directory
```

## Revision

First issue, 2026-10-08.
