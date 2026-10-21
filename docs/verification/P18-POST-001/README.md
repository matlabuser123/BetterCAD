# P18-POST-001 — Temperature / Gradient / Heat Flux Recovery

```text
MILESTONE:  P18-POST-001
SUBJECT:    from an accepted nodal temperature solution, per linear tetrahedron, grad(T) = G Te (P18-ELEM-001's G, not
            reimplemented), the Fourier flux q = -k grad(T) and its Euclidean magnitude, in K, K/m, W/m2 and W/m2;
            element-constant and unsmoothed, keyed through the thermal numbering and the mesh element order, atomic,
            bound to the solution it came from, refused when that solution is stale, and derived only
RESULT:     PASSED 2026-10-21, qualified in three presets, 4820 of 4820 each
```

## Baseline

```text
HEAD                 7a9ec06f76e72fab8c1fa4bfeebe2593ccfef453   (P18-TRANSIENT-001's record commit)
origin/main          7a9ec06f...  (pushed and verified by the owner on 2026-10-21 before this milestone began)
working tree         clean apart from the session's untracked .claude/
predecessor suite    4745 of 4745, three presets (P18-TRANSIENT-001)
this milestone       4820 tests: 4745 + 40 ThermalPost + 31 compile-fail + 4 architecture
```

## What was decided before the code

- **Nothing qualified moves.** `ThermalSolve`, `ThermalTransient`, `ThermalElement`, `ThermalData`, `ThermalDof`, the generic kernel and the structural
  wrapper are byte-identical to HEAD (FREEZE.md compares thirteen files by blob). The recovery is a new file pair plus registrations.
- **G is not rewritten.** `grad(T) = G Te` is `Tet4Geometry::gradientFrom`, the "shared gradient-recovery path" P18-ELEM-001 published for this use. The only
  physics written here is one line, `q = -k grad(T)`. An architecture gate says so and has a fixture that fails when it is not so.
- **Fourier's sign is part of the contract.** `HeatFlux3` points where heat flows (toward lower temperature); `q . grad(T) = -k |grad(T)|² <= 0`. The recovered
  quantity is the conductive flux inside the solid, not the applied surface flux of P18-LOAD-001, and it holds no convection, generation or storage term.
- **Element-constant, unsmoothed.** A linear tetrahedron has a constant gradient, so there is one value per tetrahedron. No nodal averaging, no patch recovery.
  Smoothing is a display representation for P18-VIZ-001 and may not replace this.
- **The temperature stays in Kelvin and is checked on its own,** because a Celsius offset cancels in every gradient and no flux test could see it.
- **Keyed by ordinal.** Node arrays are parallel to `ThermalDofIndex`, element arrays to `mesh.tetrahedra()`; a node is found through the numbering and an
  element through a binary search of stored ids. A raw `NodeId` or `ElementId` is never an index, and storage is `O(Ndof + Ntet)` however sparse the ids.
- **Atomic.** One invalid tetrahedron, one non-finite value, one temperature below 0 K, anywhere in any state, fails the whole recovery and publishes nothing.
- **Currentness is the solution's.** A post result records the solution's own source and answers "stale" through the transient solve's comparison, so there is
  no second rule to drift. A remesh with the same counts and ids is caught by the mesh stamp; a conductivity edit stales the result; a model older than a
  material edit is refused rather than paired with a current solution.
- **Derived only.** Not canonical, not persisted, not in the command history, and no post setting enters the source (a colour range cannot stale it).
- **Closing a recorded limitation.** P18-SOLVE-001 and P18-TRANSIENT-001 accept any finite field "including one below 0 K; the policy belongs to P18-POST-001".
  The recovery applies the one absolute-temperature rule and refuses such a field (or such a late transient state) whole.

## What this milestone adds

```text
include/bettercad/thermal/ThermalPost.hpp     ThermalPostField / FieldLocation / ThermalFieldDescriptor, PostProblem (11) / PostFailure,
                                              ThermalElementFields, recoverThermalElementFields (the algebraic seam), ThermalPostSource,
                                              ThermalPostState, ThermalPostResult, recoverSteady/TransientFromSolution,
                                              recoverSteady/TransientThermal (+ *RecoveryFailure), isCurrent / requireCurrent
src/thermal/ThermalPost.cpp                   no Jacobian, no matrix product: it calls gradientFrom
src/thermal/CMakeLists.txt                    + ThermalPost.cpp
tests/thermal/ThermalPostTests.cpp            the mathematics and the refusals, on hand meshes
tests/thermal/ThermalPostMappedTests.cpp      the authority claims, on real P16 meshes
tests/thermal/ThermalPostTestSupport.hpp      fixtures and the test's own dense 4x4 gradient oracle
tests/compile_fail/ThermalPostMisuse.cpp      31 cases
tests/architecture/CheckRecoveryOwnership.cmake + 3 fixtures + 4 tests
tests/CMakeLists.txt, tests/compile_fail/CMakeLists.txt, tests/architecture/CMakeLists.txt, tests/cli/ZeroMatchGuard.cmake   registrations; 13 filters
```

The layering checker reports **529 files, 0 violations** (527 plus the new header and source).

## The frozen contract

| | |
| --- | --- |
| gradient | `grad(T) = G Te`, K/m, `[dT/dx, dT/dy, dT/dz]`, `Te` in the tetrahedron's own node order via `elementDofs` |
| flux | `q = -k grad(T)`, W/m², `[qx, qy, qz]`, written `0 - k g` (so a zero gradient is `+0`) |
| magnitude | `|q| = hypot(qx, qy, qz)`, W/m², from the stored components |
| temperature | the solver's value, K, bit for bit, `>= 0 K`, finite |
| location | temperature per node (`ThermalDofIndex`), the rest per tetrahedron (`mesh.tetrahedra()`) |
| result | `ThermalPostResult`: 1 state without a time (steady) or N states in the solution's order and times (transient) |
| source | the solution's source by value; no post setting |
| refusals | 11 stable problems; checked in a fixed order, the inputs before the element loop |
| units | K, K/m, W/m², W/m² (the dimension of each is a `static_assert`) |

[RESULT_MODEL.md](RESULT_MODEL.md), [TEMPERATURE.md](TEMPERATURE.md), [GRADIENT_RECOVERY.md](GRADIENT_RECOVERY.md), [FOURIER_LAW.md](FOURIER_LAW.md),
[UNITS_AND_COMPONENTS.md](UNITS_AND_COMPONENTS.md), [PATCH_TEST.md](PATCH_TEST.md), [SLAB_VALIDATION.md](SLAB_VALIDATION.md), [TRANSFORMS.md](TRANSFORMS.md),
[CURRENTNESS.md](CURRENTNESS.md), [GUI_OWNERSHIP.md](GUI_OWNERSHIP.md), [DETERMINISM.md](DETERMINISM.md), [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md),
[MUTATIONS.md](MUTATIONS.md), [SCOPE.md](SCOPE.md), [FREEZE.md](FREEZE.md).

## Tests

40 ThermalPost cases (38 910 assertions), 31 compile-fail cases and 4 architecture tests, from three kinds of fixture. **Hand meshes** with expected values typed in
from the coefficients of a field or from the test's own dense solve: a 162-tetrahedron jittered patch, axis fields, a single tetrahedron with sparse handles
(7, 42, 104, 9001), jittered grids of 125 and 729 nodes. **Real meshes** through the whole chain (document, extrude, P16 mesh, mapping, `ThermalModel`,
`ThermalDofMap`, prepared loads and boundary, assembled system, the **qualified solver**, recovery): a 40 x 30 x 20 mm steel block and a 6 mm radius cylinder on
two meshes (156 and 900 nodes). **Architecture fixtures** for the ownership gate.

| Group | Cases | What it settles |
| --- | --- | --- |
| temperature | 3 | constant 325.7 K gives zero gradient; 350 K / 280 K come back as 350 K / 280 K, bit for bit; sparse handles map through the numbering |
| gradient: axes, patch | 5 | `x`, `y`, `z`, the mixed-sign affine field `(7.2, -3.5, 11.4)`, and a patch that is not one tetrahedron repeated |
| Fourier | 3 | the sign both ways, `(3, 4, 12)` has magnitude 13, conductivity scales the flux and not the gradient |
| offset, difference | 2 | a constant offset moves nothing; a difference scales linearly |
| transforms | 3 | translation, proper rotation (gradient and flux rotate), geometry scale |
| oracle, order | 2 | the general-tetrahedron dense solve to 2.45e-13 K/m; the tetrahedron's own node order in a mesh whose handles are not sorted |
| refusals, atomicity | 4 | conductivity, size, non-finite, below 0 K, a numbering of another mesh, an invalid tetrahedron, the three overflows, a bad late transient state |
| result type | 1 | ordinal keying with ids 2 and 3, every accessor, the numbering of another mesh, the seam's conductivity rule |
| slabs | 3 | a jittered 1 m slab, the block and two cylinder meshes: `qz = ±k dT/L` to within 1.7e-06 W/m² at worst (7.5e-12 relative) |
| currentness, purity | 4 | every edit, the model older than a material edit, a remesh with equal counts and ids, a failed recovery, the inputs unmutated |
| transient | 4 | the history's order and times, uniform heating (zero flux, `dT/dt != 0`), convection absent from q, the block at `t_end` |
| smoke | 2 | 729 nodes / 3072 tetrahedra; the 900-node, 4118-tetrahedron fine cylinder |
| metadata, vocabulary | 2 | every descriptor, every problem has a distinct stable name |
| determinism | 2 | 25 and 20 repeats, 0 differences; bit fingerprints equal in all presets |
| compile-fail | 31 | the three derived quantities do not convert; nothing nodal, smoothed, balanced, converted, coloured or persisted is reachable; the result is read-only |
| architecture | 4 | the ownership gate on the real tree and on three fixtures |

Measured values the suite prints: affine patch worst errors `2.73e-13` K/m, `1.36e-12` W/m² and `1.51e-12` W/m² (`|q|`); the independent dense-solve oracle
`2.45e-13` K/m; a constant 325.7 K field `3.22e-13` K/m; rotation `2.90e-13` / `1.45e-12` / `1.08e-12`; the 1 m slab `1.02e-12` K/m and `4.64e-11` W/m²
(`4500` and `-4500` W/m² for the two signs); the 20 mm block `7.28e-12` K/m and `3.27e-10` W/m² (`225 000` W/m²); the 900-node cylinder `3.73e-08` K/m and
`1.68e-06` W/m²; the transient block at `t_end` within `1.42e-06` W/m² of the slab; the insulated block heated 69.2 K with `max |q| = 9.58e-10` W/m²; the film
exchange `2671.25` W/m² against a conductive `3.27e-10` W/m² at `t = 0`.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries thirteen new filters (the total, the ten groups, the compile-fail group and the ownership gate), each with a minimum equal to
the observed count (40, 3, 6, 3, 4, 2, 7, 6, 4, 3, 2, 31, 4). `verify-harness.cmd` (11855b8f) passed beforehand and `qualify.cmd` (d313a640) is the
forty-second consecutive milestone on that byte-identical harness. The post-run script re-counted every filter in each preset (identical).

## Mutation probes

43 probe ids: **42 killed, 1 void (PM10, a probe that computed a value and discarded it), 0 survived.** Three probes survived the first pass (PM29b, PM37, PM39 — three
test gaps), each was closed by strengthening a test and rerun to a kill; four kills are libstdc++ bounds assertions inside the suite. They include a sorted node order, a
raw `NodeId` and a raw `ElementId` as an index, swapped components, a dropped sign, a missing `k`, `k²`, a Celsius temperature, a unit factor, an L1 and a
max-component magnitude, a skipped invalid tetrahedron, each finiteness gate and the below-0-K rule removed, a remesh judged by counts, the currentness checks
removed in both modes, the material cross-check removed, reversed transient states, a wrong state reported, `-(k g)`, and smoothing: [MUTATIONS.md](MUTATIONS.md).

## Adversarial review

34 questions, each answered with a test, a probe, an audit or a stated limit: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). **No production defect.** Six findings: three
test gaps (closed), one redundant branch (removed), one invalid probe (corrected), one cosmetic alignment (fixed). No gate-blocking finding.

## Scope audit

```text
G reimplemented:                       NO   (ThermalPost.cpp calls gradientFrom once; no Jacobian; architecture rule 2 + fixture)
GUI / non-thermal recomputation:       NO   (497 files outside the module scanned, 0 violations; fixture fails the gate)
convection / generation / storage in q: NO  (three tests; compile-fail THE_RESULT_HAS_NO_CONVECTIVE_FLUX, ..._STORAGE_FLUX, ..._TEMPERATURE_RATE)
nodal smoothing / averaging:           NO   (probe PM32 killed; compile-fail THE_RESULT_HAS_NO_NODAL_HEAT_FLUX, ..._SMOOTHED_FIELD)
Celsius or any unit conversion:        NO   (probes PM09, PM10b killed; compile-fail THE_RESULT_HAS_NO_CELSIUS)
heat balance, reactions, boundary flux: NO  (compile-fail; BALANCE-001 is not started)
colour, range, component selection:    NO   (compile-fail on the source and the descriptor; VIZ-001 is not started)
persistence, command, canonical data:  NO   (no serialisation symbol in either file; no setter)
any qualified module changed:          NO   (thirteen files byte-identical to HEAD)
unordered containers, threads, rand:   NO   (source audit: 0 in code)
```

## Known limitations

- **The gradient and flux are discontinuous across element faces.** That is the element's engineering result; a smoothed display is P18-VIZ-001's.
- **A body below 0 K cannot be recovered.** Only a non-physical sink produces one; the solver's own result is unchanged.
- **Each state copies the nodal temperatures** (`Ndof` doubles), so a result is self-sufficient; the solver's result remains the source of truth.
- **Reflection is not tested as a transform**: a reflected mesh inverts every tetrahedron and is refused by P18-ELEM, which the invalid-tetrahedron case shows.
- **Four probes ended in a bounds assertion rather than a failed-case count** (PM02, PM19, PM29, PM34): a detection, not a survivor. PM02 and PM29 also have clean-count companions (PM02b, PM29b).
- **No application consumes a thermal result yet**, so the ownership gate has nothing to flag in `apps/`; it is a standing constraint on P18-VIZ-001 and P18-CMD-001.

## Regression

Nothing qualified was changed, so nothing is requalified, but the full suite was run in every preset:

```text
unfiltered ctest, three presets    4820 / 4820 each   (4745 predecessor + 40 + 31 + 4)
repeat x5, release-ext, debug-ext  1739 / 1739 each   (1695 predecessor + 40 + 4)
P17 structural suite alone         415 cases, 271 414 assertions, identical in all three presets
P18-TRANSIENT, SOLVE, ASSEMBLY, BC, ELEM, DOF, MAT, DATA   passed unchanged (72, 51, 62, 92, 33, 33, 36 cases; ThermalLoad and ThermalElement through the unfiltered run)
build                              0 warnings over 701 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               529 files, 0 violations; 28 architecture tests
```

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4820 of 4820 in each preset; repeat 1739 x 5 in two
THIS MILESTONE        40 ThermalPost cases, 38 910 assertions; 31 compile-fail; 4 architecture
WARNINGS              0 over 701 objects in each preset
STAGES                17, all exit 0; 17:12:03 -> 22:51:02, 5h38m59s; run 1 of 1
MUTATIONS             43 ids, 42 killed, 1 void, 0 survived; 3 test gaps closed
ADVERSARIAL           34 questions, 6 findings, 0 production defects, 0 gate-blocking
LAYERING              529 files, 0 violations
CROSS-PRESET          bit fingerprints identical (6ba1d58f018354ca, 262ce7759cb1d043);
                      52 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and after the last repeat;
                      include 6139370a, src 9826f86b, tests 3465f39d
CONTRACT              P18_POST_CONTRACT_FINGERPRINT 7ad133be753f511d3c42953d5ca851562e3b3797
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-BALANCE-001 -- Energy / Heat Balance. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.
