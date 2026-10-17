# P18-ELEM-001 — Linear Tet4 Thermal Element

```text
STATUS:    PASS -- 21 of 21, qualified 2026-10-18 against d4800f7 plus the
           working tree: 4240/4240 x 3 presets, 1337 x 5 in two, 0 warnings
           over 673 objects, 17 stages exit 0, 4h10m35s, the eight-path
           fingerprint at the freeze == before the first build == after the
           last repeat. TWO attempts on the one frozen candidate: the first
           had one stage fail on an unrelated, documented Windows file-replace
           flake and is preserved, not counted; the second is the qualifying
           run (FREEZE.md)
MILESTONE: P18-ELEM-001, the fifth milestone of P18 — Thermal Analysis
SCOPE:     the local four-node tetrahedral thermal element: shape-function
           gradients G, the conduction matrix Ke = V k G^T G [W/K] and the
           CONSISTENT capacity matrix Ce = rho cp V/20 [2 1 1 1; ...] [J/K],
           with the orientation, degeneracy and finiteness gates. No global
           assembly, no boundary condition, no load, no solve, no heat-flux
           post-processing.
```

## Baseline

```text
HEAD at start      d4800f79e394d8a893526707a82c68b1600095df  (P18-DOF-001's
                   documentation record, on top of daea78b)
HEAD^{tree}        349d247d19aa815915947a2a6cce60209b313b02
origin/main        a3b721b150338cc1cbd24ee592a11a70a6196c0c  -- EIGHT COMMITS
                   BEHIND: no P18 commit had reached the remote when this
                   milestone started; the push is the owner's
working tree       clean but for the untracked .claude/ settings directory;
                   git diff, git diff --cached and git diff --check empty
P18-DOF-001        PASS, 12 of 12, 4163/4163 x 3     P18-MAT-001   PASS, 19 of 19
P18-DATA-001       PASS, 19 of 19                     P18-ARCH-001  PASS, 19 of 19
P17 / P16          QUALIFIED
authorized         2026-10-17, "start", with the owner's P18-ELEM-001 brief
```

## What the audit decided before any code was written

**P16 supplies the geometry and the rule.** `Tetrahedron` stores four
handles in generator order with "orientation meaningful and never
canonicalised"; `Node::position` is a `Point3D` of `Length`, SI metres;
`meshing::signedVolume` is `(1/6)(p2-p1).((p3-p1)x(p4-p1))` with "no
`std::abs` anywhere: the sign IS the orientation".

**P17-ELEM-001 is the precedent and cannot be called.** Its Jacobian
convention, determinant grouping, `J^-T` gradient mapping and
no-abs/no-reorder/no-symmetrise/no-regularise rules are adopted here; its
code lives in `structural/`, which `thermal/` may not include (ADR-050), and
it carries `B`, `D`, `E`, `nu` that are structural. The Jacobian helpers are
in an anonymous namespace there, so there is no generic layer to reuse;
extracting one would re-qualify four P17 milestones, which the brief forbids
doing gratuitously. **Shared Tet geometry: reused convention, not shared code;
`meshing::signedVolume` agrees bit for bit.**

**ADR-049 left one decision here:** consistent versus lumped capacity, with
the requirement that it be one stated policy and `C` symmetric positive
definite. **Decided: CONSISTENT.** [CAPACITY.md](CAPACITY.md).

**One inherited rule was wrong and is not inherited.** P16 and P17 call a
tetrahedron degenerate only when its volume is exactly zero. This milestone's
own coincident-corner test showed the rule accepting zero-volume tetrahedra
(885 of 2000 exactly-degenerate fixtures) because the determinant of
non-integer coordinates is rounding noise, not zero. The kernel uses a
derived, scale-aware bound instead. [DEGENERACY.md](DEGENERACY.md).

## What this milestone adds

```text
Tet4Geometry        V, det J and G (3 x 4) of one VALID tetrahedron; no public
                    constructor, so possession proves validity;
                    gradientFrom(Te) = G Te, the shared path for P18-POST-001
Tet4Conduction      Ke = V k G^T G, 4 x 4, W/K; from geometry and k ONLY
Tet4Capacity        Ce = rho cp V/20 [2 1 1 1; ...], 4 x 4, J/K; from geometry
                    and {rho, cp} ONLY; total() = rho cp V
cornersOf           the four positions of a mesh Tetrahedron, in its own order
ElementProblem      seven classified refusals, each reachable and tested
ThermalConductance, HeatCapacity, InverseLength     dimensioned aliases whose
                    reasoning is a static_assert
```

## The frozen formulation

[FORMULATION.md](FORMULATION.md), written before the code:

```text
local order     [n1,n2,n3,n4] as P16 stores them; Te = [T1,T2,T3,T4]^T
J               columns are the edge vectors from corner 1; J[i][j] = dx_i/dxi_j
det J, V        e1.(e2 x e3), P16's grouping; V = det J / 6; NO absolute value
G               3 x 4, row = axis, column = corner; grad T = G Te; J^-T mapping
Ke              V k G^T G  [W/K]                      exact, G is constant
Ce              rho cp V / 20 * [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2]   [J/K]
policy          CONSISTENT; no lumped variant, no setting
orientation     det < -B inverted, REFUSED never reordered; |det| <= B degenerate
B               16 eps |e1||e2||e3|   (derived: analytic bound 4 eps, measured 1.01 eps)
units           coordinates m, V m^3, G 1/m, k W/(m K), rho kg/m^3, cp J/(kg K)
```

## Tests

```text
tests/thermal/ThermalElementTests.cpp        60 cases, 11 402 assertions
  geometry         unit tet by hand; the brief's general tet against the
                   independent [1 x y z] inverse; bitwise agreement with
                   meshing::signedVolume; sum grad Ni = 0; T = x, y, z;
                   the general affine field; the constant field
  orientation      inverted; coplanar; every coincident pair; both sides of
                   the noise bound at three scales; scale-invariant verdicts;
                   2000 exactly-degenerate fixtures; non-finite coordinates;
                   subnormal needles; overflow
  conduction       hand matrix; oracle; symmetry; null mode; row sums; PSD;
                   rank 3; the energy identity with an independent right-hand
                   side; conductivity scaling; invalid k
  capacity         hand matrix; oracle; the full consistent matrix; symmetry;
                   SPD; full rank; row sums; total; the one-point and lumped
                   traps; density and specific-heat scaling; invalid rho, cp;
                   overflow
  transforms       translation (and 1e6 m); proper rotation; gradient
                   transport; reflection; even permutation covariance; the six
                   odd permutations refused
  scaling          V s^3, G 1/s, Ke s, Ce s^3 at five scales
  integration      sparse-handle mesh in element order with the DOF map;
                   steady material (k only); transient material (k, rho, cp);
                   field-swap protection; dimensional analysis
  determinism      200 repeats; no hidden state; a bit fingerprint
tests/compile_fail/ThermalElementMisuse.cpp  17 cases + 1 control
  a double is not a conductivity; a density is not a conductivity; the
  capacity takes no conductivity; the pair is not two doubles and rho/cp do not
  swap; W/K is not J/K; a gradient is not a temperature; corners are not
  NodeIds or unknowns; temperatures are not doubles; the three value types
  cannot be conjured; no conductivity on the geometry, no capacity total on
  Ke, no lumped variant on Ce
```

## Zero-match protection

```text
ctest -R 'unit\.ThermalElement'                                  Total Tests: 60 (minimum 40)
  ... _(UnitTet|GeneralTet|ShapeGradient|Affine|Constant)        10 (minimum 5)
  ... _(Inverted|Coplanar|Coincident|NearDegenerate|NonFinite)    6 (minimum 4)
  ... _Conduct                                                    9 (minimum 8)
  ... _(Capacity|OnePoint|Density|SpecificHeat)                  14 (minimum 8)
  ... _(Translation|Rotation|Gradient|Reflection|Even|Odd)        6 (minimum 5)
  ... _(GeometryScaling|ConductivityScaling)                      3 (minimum 2)
  ... _(Repeat|NoHidden|BitFingerprint)                           3 (minimum 3)
ctest -R 'compile_fail\.thermalelem\.'                           17 (minimum 8)
the earlier thermal filters                                      unchanged
an impossible filter                                             0, so the counter is real
```

## Mutation probes

33 distinct mutations of the kernel, **33 killed, 0 survived, 0 void**
(39 runs: five probes did not compile under `-Werror` and were reformulated,
one survived and drove a test strengthening, all six were rerun and killed).
The table, with the killing cases, is in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). They include: abs of the
determinant; `J^-1` for `J^-T`; swapped y/z; sorted corners; transposed `J`; a
millimetre factor; `V = det J`; the inverted and even-permutation checks; the
exact-zero and absolute degeneracy rules; the volume and gradient gates;
`Ke` without `V`, with an epsilon, averaged or with a broken null mode; every
shape of wrong `Ce` — lumped, one-point centroid, off-diagonal = diagonal,
`/10`, wrong ratios, `rho` or `cp` omitted, `rho + cp`, `abs` of either; and
the finiteness gates.

## Adversarial review

```text
37 attacks from the brief + 7 of the reviewer's     all answered by measurement
findings                                             6 (one production defect)
production defects                                   1 -- the exact-zero degeneracy
                                                     rule, found by this milestone's
                                                     own test and fixed before the freeze
gate-blocking                                        0
```

## Known limitations

```text
Isotropic constant k, constant rho and cp only. Anisotropic conductivity
  (Ke = V G^T K G with a tensor) and temperature-dependent properties are
  DEFERRED -- ADR-049 refuses them and P15 stores one scalar.

CONSISTENT capacity is the whole policy. Its known cost -- a possibly
  non-monotone first backward-Euler step at small time steps -- is a property
  of the time integration and is P18-TRANSIENT-001's to measure.

The degeneracy bound is a refinement of P16's and P17's rule. The same latent
  gap exists in P17's Tet4Element.cpp and is RECORDED for the owner, not
  changed: doing so would re-qualify P17-ELEM-001, P17-ASSEMBLY-001,
  P17-POST-001 and P17-REFMOD-001.

A sliver (1000:1) is computed, not refused; its conditioning (about 1e6) is
  visible in the eigenvalue tests. Mesh acceptance is P18-VALID-001's.

An offset of 1e6 m costs about 8e-11 of Ke accuracy (measured, recorded, not
  asserted tight): double precision resolves ~1e-10 m there.

Cross-preset floating-point identity is measured, not promised
  (DETERMINISM.md).

This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each from a fresh configure and a clean build, each running the
full unfiltered ctest; then the repeat set five times in two presets. Logs are
under [qualification/](qualification/), the stage timeline in
`qualification-times.txt`, the measurements in `postrun-summary.txt`, and the
failed first attempt in `qualification/run1-failed/`. **The numbers below are
the qualifying run, attempt 2.**

```text
preset             configure  clean  build (warnings/objects)  no-op  ctest            time
debug-ext          0          0      0   (0 / 673)             0      4240/4240  0     2301.56 s
release-ext        0          0      0   (0 / 673)             0      4240/4240  0     2087.88 s
debug-shared-ext   0          0      0   (0 / 673)             0      4240/4240  0     2229.75 s
repeat release-ext                                                    1337 x 5   0     1192.47 s
repeat debug-ext                                                      1337 x 5   0     1336.00 s

4240 = 4163 (P18-DOF-001's qualified count, every one still present and
       passing) + 60 unit.ThermalElement + 17 compile_fail.thermalelem
1337 = the repeat filter's selection, recorded by the run, not predicted;
       1277 at P18-DOF-001 + 60 ThermalElement cases
no-op rebuild: one edge ("no work to do") in every preset
harness stderr: 268 lines, every one git's CRLF notice, 0 others
debug-shared-ext: 11 libbettercad_*.dll, libbettercad_thermal.dll among them

ThermalElement_* on each preset's own test binary, after the run:
  debug-ext          60 / 60   11 402 assertions
  release-ext        60 / 60   11 402 assertions
  debug-shared-ext   60 / 60   11 402 assertions
  the suite's 74 message lines (every measurement quoted in this directory)
  are IDENTICAL across the three presets once sorted (Catch2 shuffles order)
  bit fingerprint of V, det J, G, Ke and Ce over 14 fixtures:
      a59150e680dd9121 in debug-ext, release-ext and debug-shared-ext
      -- BITWISE IDENTICAL across the three presets
[thermal][dof] 33 / 33 (21 480), [thermal][material] 31 / 31 (1529),
  [thermal][data] 36 / 36 (2349), [structural][elem] 29 / 29 (4384): all
  unchanged from their own qualifications, in all three presets
zero-match counters on every preset: ThermalElement 60, its seven group
  filters 10 / 6 / 9 / 14 / 6 / 3 / 3, thermalelem compile_fail 17, ThermalDof
  33, ThermalMaterial 31, ThermalData 36, Thermal 195, Tet4Element 29,
  thermaldof 15, architecture 19
layering after the run: 509 files, 0 violations
```

**The first attempt.** Stage `debug-shared-ext ctest` exited 8 on one case,
`DrawingCli_TheReportKeepsItsColumnsApartWhateverAnObjectIsCalled`, with
`cannot replace '...part.bcad': Permission denied`, the Windows
sharing-violation flake recorded since P14 and in INFRA-QT-DEPLOY-001. The case
passes 30 of 30 in isolation on that preset, passed in the other two presets of
that attempt and in all three of this one, and none of its sources changed.
It is disclosed in full in [FREEZE.md](FREEZE.md); it was neither excluded nor
retried inside the harness, and the unrelated defect it points at (a single
un-retried `rename` in `writeFileAtomically`) remains the open item
INFRA-QT-DEPLOY-001 left.

The P16 and P17 suites, and P18-ARCH/DATA/MAT/DOF's, ran unchanged: no file
under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
is in this change, and the fingerprint proves the qualified tree is the
frozen one.

## Result

```text
P18-ELEM-001   PASS   2026-10-18
  21 of 21 TODO items; 60 unit cases, 17 compile-fail cases
  33 mutation probes killed (0 survived, 0 void at the end); 44 adversarial
    attacks answered; 1 production defect, found by the milestone's own test
    and fixed before the freeze (the inherited exact-zero degeneracy rule)
  4240/4240 x 3 presets; 1337 x 5 in two; 0 warnings; 17 stages exit 0;
    attempt 2 of 2 on the one frozen candidate (attempt 1: one unrelated
    flake, preserved)
  fingerprint  include 655275dd  src 762b0a9c  tests 6dd66736  apps a95876bc
               examples a955cf65  cmake 5a382115  CMakeLists.txt 3ec3c10f
               CMakePresets.json 3229f0f8   at the freeze == before == after
  element results BITWISE IDENTICAL across debug-ext, release-ext and
    debug-shared-ext (fingerprint a59150e680dd9121)
  the commit's subtree hashes, read back after the commit, are recorded in
    FREEZE.md by the follow-up documentation commit
Next: P18-LOAD-001 -- Thermal Loads. NOT AUTHORIZED.
```

## Revision

First issue, 2026-10-18.
