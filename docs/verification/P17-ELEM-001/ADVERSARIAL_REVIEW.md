# P17-ELEM-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the Tet4 element kernel before a global
          assembly and a solver are built on it
QUESTIONS: 29 from the brief, plus 4 of the reviewer's own
FINDINGS: 3 -- 1 latent production defect, 1 claim corrected, 1 probe design
          error of mine
PRODUCTION DEFECTS: 1, found and fixed in this milestone
GATE-BLOCKING: 0
```

## Findings

### F1 — the determinant was algebraically right and one ulp from P16's (FIXED)

`TET4_DERIVATION.md`'s first draft claimed that `det J` was
"character-for-character the expression `meshing::signedVolume` computes", and
the test asserting bit-for-bit agreement **failed**:

```text
Tet4Element_VolumeAgreesWithTheMeshingSignedVolume
  CHECK( k.volume().si() == p16.si() )
  with expansion: 0.21253133333333338 == 0.21253133333333332
```

One ulp, on the skew tetrahedron. The first implementation expanded cofactors
along `J`'s first row; `signedVolume` computes `cross` then `dot`. Same
algebra, different association, and floating-point addition is not
associative — so the claim was **false**, asserted from an algebraic identity
rather than measured. That is the same failure shape as
`a-quoted-figure-is-an-unverified-figure`, arriving in a numerical form.

**Why it was fixed rather than the test relaxed.** The two functions are
predicates on the *same* boundary. `MeshValidation` accepts a tetrahedron when
its signed volume is `> 0` and this kernel refuses one when its volume is
`<= 0`, so a tetrahedron whose volume rounds to within an ulp of zero could be
**data-valid for the mesh and inverted for the solver** — a qualified mesh
containing an element the solver refuses. Practically unreachable for CAD
geometry; structurally real, and removable for free.

The determinant is now grouped exactly as P16 groups it, with `cross` and `dot`
read from `src/meshing/Mesh.cpp` rather than reconstructed. Measured
afterwards: **exactly zero** disagreement on every element of RM-MESH-01,
RM-MESH-03 and RM-MESH-07, asserted as `== 0.0` and not with a tolerance.

Classified as a **latent production defect** rather than a documentation slip,
because the divergence was in the code: the claim merely described it wrongly.

### F2 — two mutation probes that cannot be killed, for two different reasons (RECORDED)

`M7` replaces `volume` with `std::abs(volume)` in the stiffness computation and
**survived**. `M8` adds `Ke = 0.5 (Ke + Ke^T)` after the triple product and
**survived**.

Neither is a test gap, and neither was explained away:

```text
M7   By the time computeTet4Stiffness runs, its argument is a
     Tet4Kinematics -- a type with a private constructor and one friend, so
     possessing one IS the evidence that the volume is positive and finite.
     abs() of a positive number is that number, so the mutation is a NO-OP.
     The thing M7 was aiming at IS tested, by M1: putting abs() at the
     PREDICATE, where orientation is actually decided, is killed by three
     tests.
M8   The measured symmetry residual of Ke is EXACTLY ZERO on the chosen
     fixtures. Averaging a matrix with its own transpose when the two are
     bit-identical is a no-op by arithmetic. The probe's survival is
     therefore the evidence the brief asks for -- that nothing needs
     repairing -- rather than a hole.
```

Both are **unkillable by construction**, and a test contrived to kill them
would be asserting that a no-op is a no-op. What was done instead was to add
probes that *are* reachable and test the same properties where they live:
`M11`, `M12` and `M13` remove the orientation, degeneracy and non-finite
branches outright.

This is the second milestone running in which a surviving mutation pointed at
something other than a missing test, and the lesson is the same one
`bettercad-mesh-quality-conventions` records: a survivor means the branch is
unreachable, the code is a no-op, or the test is missing, and the three have
different fixes.

### F3 — my own probe design error: M6 was killed by the compiler, not the suite (FIXED)

`M6` drops `V` from `Ke`, which is the mutation that leaves every symmetry and
rigid-body property intact and makes the dimensions wrong. It came back
`KILLED-BY-COMPILER`:

```text
Tet4Element.cpp:460:18: error: unused variable 'volume' [-Werror=unused-variable]
```

A true kill, and a **useless** one: it says `-Werror` noticed an unused local,
not that any test can see a missing volume factor. The probe was rewritten as
`sum + 0.0 * volume`, which compiles and is numerically identical to dropping
the factor, so the suite has to catch it on its own account.

## The brief's 29 questions

**Can an inverted Tet be accepted because P16 usually fixes it?** No, and the
brief is right that this is the question to ask. P16's Netgen adapter applies
one node swap, so a mesh arriving here is positively oriented — and the kernel
checks anyway, because the guarantee covers meshes that came through P16 and
not synthetic fixtures, a future caller, or an upstream regression.
`tet4GeometryProblem` computes the signed volume itself. Three tests cover it
(one swap, three odd permutations, a reflection) and `M11` confirms the branch
is load-bearing.

**Can `abs(volume)` hide an inverted element?** Not where it matters. `M1` puts
`std::abs` on the determinant inside the predicate and is killed by three
tests. `M7` puts it after the gate, where the argument is already a validated
`Tet4Kinematics`, and survives as a no-op — see F2. Searched: **zero**
occurrences of `abs` in `src/structural/Tet4Element.cpp` outside two comments
saying why there are none.

**Can a degenerate Tet create Inf/NaN B and still return success?** No.
Degeneracy is refused before the inverse is formed, and after the gradients are
computed every component is checked finite — `NonFiniteResult`, a **result**
check rather than an invented input tolerance. Four degenerate fixtures are
tested (coplanar, coincident, collinear, and the reflection case) and none
produces a value.

**Can a near-degenerate Tet bypass the scale-aware validity rule?** There is no
scale-aware rule to bypass, and that is the audit's finding rather than an
omission: `MeshValidation.hpp` states "there is no tolerance in this file … A
thin tetrahedron is data-valid and is P16-QUALITY-001's to complain about." So
a 1000:1 thin tetrahedron is **accepted**, with finite gradients and a finite
`Ke`, and there is a test asserting that. Refusing it would make a qualified
mesh unsolvable, and `P17-VALID-001` owns acceptance policy.

**Can `J` and `J^T` conventions be mixed?** The convention is frozen in
`TET4_DERIVATION.md` — `J[i][j] = dx_i/dxi_j`, columns are the edge vectors —
and the reference element gives `J = I`, which would hide a transpose, so the
discriminating fixture is the **skew** one. `M2` swaps the index order in the
gradient loop and is killed by seven tests including the independent reference.

**Can gradients use `J^-1` when `J^-T` is required?** That is exactly `M2`.
Killed. And the independent reference cannot mirror the error, because it never
forms a Jacobian at all — it reads the gradients out of a 4x4 inverse.

**Can engineering-shear B be paired with tensor-shear D?** `M10` halves the
`gxy` row of B, which is the tensor-shear convention, and is killed by five
tests.

**Can D use `2 mu` on the shear diagonal by mistake?** `M3`. Killed by five
tests, including the dedicated `tau = mu gamma` regression that asserts the
value **and** asserts it is not `2 mu gamma`.

**Can XY/YZ/ZX ordering differ from P17-DATA?** The row index of B *is* a
`TensorComponent`, cast from the loop variable, so there is no second ordering
to differ. `Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering` additionally
`STATIC_REQUIRE`s all six enumerator values, so a reordering in P17-DATA breaks
this test rather than silently permuting stresses. `M4` swaps the `gyz` and
`gzx` rows and is killed by three tests.

**Can local element DOF ordering differ from P17-DOF?** `localDofIndex` is
written as `kDofsPerNode * node + offsetOf(component)` — both P17-DOF's own —
so there is nothing to drift. The test also checks it against the **global**
map: for a four-node mesh, `localDofIndex(node, c) + 1 == map.indexOf(...)`.
`M5` switches to component blocking and is killed by eight tests.

**Can Ke be made symmetric artificially and hide a formula error?** There is no
symmetrisation: searched, and the only occurrence of `0.5 * (` in the file is a
comment saying why. `M8` adds one and survives as a no-op, which is the
evidence that nothing is being repaired — see F2.

**Can diagonal regularisation hide missing rigid-body modes?** `M9` adds
`1e-3 V` to the diagonal and is killed by four tests — the two scale laws, the
independent reference and the analytical case. Notably it is **not** killed by
the rigid-body test, because `1e-3` relative is inside that test's own band;
the scale laws are what catch it, which is worth recording because it shows the
suite's coverage does not rest on one assertion.

**Can a free Tet have fewer or more than six rigid-body modes?** The six are
tested **explicitly** by their analytically constructed vectors — a null space
of the right dimension could still be the wrong null space — and then counted
two ways: an eigendecomposition (6 inside the zero band, 6 positive) and an
SVD rank of 6. The separation is fifteen orders of magnitude, so the `1e-10`
relative band is not a tuned number.

**Can a materially negative eigenvalue be dismissed as roundoff?** No: the test
`FAIL`s on any eigenvalue outside the band on the negative side, and names it
with the largest for scale. Two of the six zero modes *are* computed negative
(`-7.5e-6`, `-2.3e-6` against a largest of `2.1e11`), which is why the band
exists and why it is relative.

**Can translation change Ke?** No, and the tolerance is stated per offset
rather than assumed uniform: 1e-14 relative at metres, 1e-12 at kilometres,
1e-9 at 1e6 m. The loosening is subtractive cancellation, measured and recorded
as a limitation rather than wished away.

**Can uniform geometry scaling violate `Ke ∝ s`?** Five scales from 1e-3 to
1e3, with `V ∝ s^3`, `B ∝ 1/s` and `Ke ∝ s` each asserted to 1e-12 relative.
This is the sharpest units check in the milestone and it kills `M6` and `M9`.

**Can scaling E violate `Ke ∝ E`?** Four factors, 1e-3 to 1e3, asserted to
1e-12.

**Can a proper rotation change strain energy?** No. And the test asserts
**covariance**, `Ke_rot = T Ke T^T`, rather than invariance — requiring
`Ke_rot == Ke` would be wrong, since the global component basis turns with the
geometry. Volume, eigenvalues and strain energy under the rotated displacement
are the invariants, and all four are checked.

**Can a reflected Tet be accidentally accepted?** No. `det R = -1` with the node
order unchanged flips the signed volume and is refused; swapping a node pair
restores validity, which is what makes that a statement about orientation.

**Can a unit mismatch of mm coordinates and Pa modulus survive tests?** No, and
the test is constructed to catch exactly that: the same physical element built
from `Length::fromSi(0.1)` and from `100_mm`, with `E` as `2e11 Pa` and as
`200_GPa`, gives `Ke` equal to **1e-15 relative; measured 0**. `Quantity`
converts at construction and stores SI, so the conversion happens once, at the
boundary.

**Can the independent reference accidentally call production helpers?** It
cannot reach them: `reference()` is declared above the production readers in
the file's anonymous namespace and uses none of them, and its only production
dependency is reading `Point3D::x/y/z` off its argument. The three enforcement
mechanisms are written out in `INDEPENDENT_REFERENCE.md`.

**Can expected matrices be generated from the production code under test?** The
unit-tetrahedron expectation is built a **third** time — integer gradients
written out, the B block written out again, `D` as
`diag(1,1,1,1/2,1/2,1/2)` — so that case has three independent descriptions.
No production-generated matrix is pasted anywhere.

**Can invalid material produce NaN Ke instead of failure?** No.
`isotropicElasticity` refuses a non-finite `lambda` or `mu`, which covers
`nu = 0.5`, `nu = -1` and NaN. The admissible **range** is deliberately not
re-checked — see the reviewer's own question below.

**Can P17-ELEM duplicate P16 quality metrics?** Searched: no aspect ratio, no
radius ratio, no dihedral angle, no quality threshold anywhere in the module.
Element math needs valid geometry; `P16-QUALITY-001` measures and
`P17-VALID-001` decides.

**Can P17-ELEM repair P16 mesh nodes?** It cannot see a mesh. The kernel takes
`std::array<Point3D, 4>` by const reference and returns a value; there is no
`Mesh&`, no node swap and no mutation. The reference-mesh tests read
coordinates through `Mesh::findNode` and pass copies.

**Can B/D be reimplemented later in P17-POST with different conventions?** That
is the risk the public API exists to remove. `Tet4Kinematics::strainFrom` and
`ElasticityMatrix::stressFrom` are public and are the shared paths, and the
header says why in as many words. It is not *enforced* — a later milestone
could still write its own — and that is recorded as a known limitation rather
than claimed as a guarantee.

**Can P17-ELEM depend on GUI, CLI or persistence?** No. The module links
`core`, `features` and `meshing` only, the layering check passes, and
`src/io/`, `apps/` and the renderer contain zero references to any of these
types.

**Can deterministic tests run zero cases under a filter?** Counted with `-N`
before every run: `Tet4Element_` selects **29**. The final regression is
unfiltered.

## Four of the reviewer's own

**Should the element have refused an out-of-range but finite material?** This
was the sharpest call in the milestone. `E <= 0`, `nu > 0.5` and `nu < -1` give
a finite `D` that is **not** positive definite, so `Ke` would be finite and
indefinite — a correctness disaster for a solver. The temptation is to check
the range here.

It was rejected, with the reason recorded in the header: P15 refuses an
unusable `E` or `nu` **at the point of entry**, which P17-MAT-001 established
and proved by eleven failing tests, so a material resolved through
`StructuralMaterial::elastic()` cannot be out of range. Adding the check here
would be a second definition of admissibility — precisely the drift ADR-028 and
that milestone's F1 warn against — and the intrinsic form (`mu > 0` and
`K > 0`) *is* the range check, just written differently. What is checked is
finiteness, because `LinearElasticConstants` is a plain struct a caller can
fill by hand and a NaN must not reach `Ke` silently. Recorded as a known
limitation with its reachability stated, not as a guarantee.

**Was avoiding Eigen in production the right call, or just conservative?**
Three independent reasons, and the third is the decisive one. The public API
must carry `Ke`, `B` and `D` to P17-ASSEMBLY and P17-POST, and **no public
header in this repository includes Eigen** — it is a `PRIVATE_LINK` of both
`sketch` and `assembly` — so these types could not have been Eigen types
anyway. What the kernel needs is a 3x3 determinant and inverse and two fixed
products: arithmetic, not a library. And `src/structural/CMakeLists.txt`
records that admitting Eigen to this module is **P17-SOLVE-001's decision**,
carrying a licence question the owner must answer because the project has no
licence. Taking it here would have pre-empted both. The tests use Eigen freely,
which is also what makes the independent reference take a different route.

**Is the bit-exact volume agreement over-tight?** It is asserted as `== 0.0`
across every element of three reference meshes, which looks like the kind of
assertion that breaks on a compiler change. It is defensible because it is not
a numerical coincidence: the two functions compute the same expression with the
same association over the same inputs, so they are the same sequence of
floating-point operations. If a compiler ever reassociated one and not the
other the test would fail and the *right* response would be to look, because
the boundary disagreement F1 describes would be back. A relative tolerance
there would hide exactly what the test is for.

**Does anything here depend on P16 having corrected Netgen's orientation?** No,
and that was checked rather than assumed, because it is the one upstream
guarantee the kernel is explicitly told not to lean on. The validity predicate
is computed from the four coordinates the caller supplies. The reference-mesh
pass confirms the two agree in practice — every element of three qualified
meshes is accepted — but the unit tests reach the inverted, degenerate and
reflected cases through synthetic fixtures that never went near P16, so the
refusals are tested on their own account.

## Result

```text
QUESTIONS:                      29 + 4 = 33
FINDINGS:                       3
PRODUCTION DEFECTS:             1  (F1 -- the determinant's association
                                    diverged from P16's by one ulp, which made
                                    a boundary disagreement possible; fixed,
                                    and the agreement is now exact)
CLAIMS CORRECTED:               1  (F1's "character-for-character", asserted
                                    from algebra and false in floating point)
PROBE DESIGN ERRORS OF MINE:    1  (F3 -- M6 was killed by -Werror rather than
                                    by the suite, which is a useless kill)
UNKILLABLE PROBES, RECORDED:    2  (F2 -- M7 and M8, both no-ops for stated
                                    structural reasons, with reachable probes
                                    added in their place)
GATE-BLOCKING:                  0
VERDICT:                        PASS
```
