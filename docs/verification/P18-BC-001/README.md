# P18-BC-001 — Thermal Boundary Conditions

```text
MILESTONE:  P18-BC-001
SUBJECT:    prescribed temperature T = T_bar and convection -k grad(T).n = h (T - T_inf),
            resolved from canonical CAD references against the current P16 mesh into
            exact temperature constraints and consistent Robin boundary terms
RESULT:     PASSED 2026-10-19, qualified in three presets, 4448 of 4448 each
```

## Baseline

```text
HEAD                 b4714a487c04a3ac8ef2d294bc40f26585ec2f8e   (P18-LOAD-001's record commit)
origin/main          b4714a48...  (the owner's push of the two P18-LOAD-001 commits, verified)
tree                 4f60427134e588f647421cb2628ff4b299429114
working tree         clean apart from the session's untracked .claude/
predecessor suite    4325 of 4325, three presets (P18-LOAD-001)
this milestone       4448 tests: 4325 + 92 ThermalBC cases + 31 compile-fail cases
```

## What the audit decided before any code was written

Full table in [BC_MODEL.md](BC_MODEL.md). The decisions that shaped the work:

- **The canonical conditions already existed.** P18-DATA-001 froze
  `ThermalBoundaryCondition` as either a `PrescribedTemperature {FaceName,
  Temperature}` or a `ConvectionBoundary {FaceName, HeatTransferCoefficient,
  Temperature}`, in kelvin and W/(m² K), and P18-DOF-001 froze the constraint map.
  This milestone adds nothing to the persisted model. A canonical condition holds no
  facet, node, element or unknown.
- **`FaceName` is the GeometryReference**, and P16's `boundaryFacetsOf`, reached
  through `ThermalTarget` (written by P18-LOAD-001 for this milestone to share), is
  the only thing that resolves it. Nothing in P16, P17 or any earlier P18 milestone
  changed, so nothing needed requalifying.
- **P17's restraint logic is not reused.** A structural restraint holds a
  displacement at zero and records only which unknowns are fixed. A temperature is
  arbitrary, so the constraint carries its value, and 350 K stays 350 K.
- **Two kinds, two combining rules.** Equal prescriptions collapse into one
  constraint and contradictory ones are refused; convections are physical exchanges
  and always add, even when identical.
- **`h = 0` stays refused canonically** (P18-DATA-001's committed rule). The kernel
  and the integration seam are total over `h >= 0`. See BC_MODEL.md for the reason
  and the one-line change if the owner wants it the other way.

## What this milestone adds

```text
include/bettercad/thermal/ThermalConvection.hpp   the local Robin kernel: Tri3Convection, Kconv 3x3 and Qconv 3
include/bettercad/thermal/ThermalBoundary.hpp     constraints, convection facets, PreparedThermalBoundary, currentness
src/thermal/ThermalConvection.cpp                 closed-form terms, validity and overflow gates
src/thermal/ThermalBoundary.cpp                   unique-node constraints, attribution, conflicts, preparation
tests/thermal/ThermalBoundaryTests.cpp            the mathematics and the seams, on hand meshes
tests/thermal/ThermalBoundaryMappedTests.cpp      the authority claims, on real P16 meshes
tests/thermal/ThermalBoundaryTestSupport.hpp      fixtures and test-side arithmetic
tests/compile_fail/ThermalBoundaryMisuse.cpp      31 cases
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or
`apps/` changed. The thermal layer includes only core, features and meshing
(ADR-050); the layering checker reports **517 files, 0 violations**.

## The frozen formulation

| | |
| --- | --- |
| sign | `-k grad(T).n = h (T - T_inf)`, `n` outward; positive `h (T - T_inf)` is heat leaving the body |
| weak form | `(K + Kconv) T = Q + Qconv`, `Kconv = integral h N^T N dA`, `Qconv = integral h T_inf N^T dA`, derived in [WEAK_FORM.md](WEAK_FORM.md) |
| convection, per linear triangle | `Kconv = (h A / 12) [2 1 1; 1 2 1; 1 1 2]` W/K, `Qconv = (h T_inf A / 3) [1 1 1]` W: exact, consistent, no conductivity |
| prescribed temperature | one exact constraint per unique boundary unknown, carrying `T_bar` in kelvin; no penalty |
| conflicts | same unknown, different value: refused, exact equality, reported per pair of conditions |
| overlap | prescribed temperature and convection on one face are both kept; convections add |
| no Dirichlet | valid at this layer |
| units | K, K, W/(m² K) in; W/K and W out |

[SIGN_CONVENTION.md](SIGN_CONVENTION.md), [UNITS.md](UNITS.md) (with the tolerance
table, one class per row), [DIRICHLET.md](DIRICHLET.md),
[CONVECTION.md](CONVECTION.md), [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md) (with
the source and persistence audits), [REMESH_CURRENTNESS.md](REMESH_CURRENTNESS.md),
[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md), [SCOPE.md](SCOPE.md).

Failure is classified by fourteen named problems, every one reachable and tested, and
is **atomic**: one bad condition refuses the whole set and nothing is published.

## Tests

92 cases, 4785 assertions, built from two kinds of fixture. **Hand meshes** go
through the kernel and the seams with expected values typed in the test or computed
from the coordinates there, with no production helper as oracle. **Real meshes** go
through the whole chain (document, extrude, P16 mesh, mapping, `ThermalModel`,
`ThermalDofMap`): a 40 × 30 × 20 mm block and a 6 mm radius cylinder with a coarse
and a fine control whose boundary meshes genuinely differ.

| Group | Cases | What it settles |
| --- | --- | --- |
| definition | 5 | the canonical records, kelvin, units, sign |
| Dirichlet | 6 | unique nodes once, value preserved, ordering, sparse unknowns |
| conflict | 8 | duplicates collapse, contradictions refused, exact equality, edge overlap |
| Robin analytical | 14 | the hand triangle, equilibrium, above/below ambient, shared nodes, winding |
| Robin traps | 3 | the centroid, lumped, sign and factor mutants are rejected by the same oracle |
| scaling | 5 | `s^2`, `h`, `T_inf`, no conductivity |
| mapping | 9 | resolution through P16, unresolved, malformed, empty, no nearest face, all-or-nothing, overlap policy |
| remesh and currentness | 12 | same counts, same ids, a different body, edits, material/load/solver do not invalidate |
| invalid input | 20 | `h`, `T_bar`, `T_inf` policy, overflow, identities, foreign numbering, zero area |
| sparse ids | 2 | node ids 7, 104, 9001, 500000 and 4 294 967 294 |
| superposition, purity, determinism, scope | 8 | additive convection, nothing mutated, 50 repeats, a bit fingerprint, no penalty |
| compile-fail | 31 | dimensioned values do not convert, nothing mesh-local, no conductivity, no penalty, no matrix |

Measured values the suite prints: the hand triangle gives `Kconv = [2 1 1; 1 2 1;
1 1 2]` W/K and `Qconv = [1200 1200 1200]` W exactly; at 350 K against a 300 K
ambient the outward residual is +600 W and at 250 K it is -600 W; the block's end
cap (two facets, four unique nodes of nine) carries `h A = 0.03` W/K and
`h T_inf A = 8.7945` W at `h = 25`, `T_inf = 293.15 K`; the cylinder cap is
-0.507 % against the CAD disc on the coarse mesh (36 cap nodes) and -0.032 % on the
fine one (144 cap nodes), which is the discretisation of the mesh and nothing else.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries sixteen filters (the total, the thirteen
groups of the brief's list and its extras, the scope case and the compile-fail
group), each with a minimum equal to the observed count, and every one of the 92
cases belongs to at least one named group. `verify-harness.cmd` (11855b8f) passed
beforehand and shows a filter that matches nothing is counted as a failed stage.

## Mutation probes

58 distinct mutations in 65 runs: **58 killed, 0 survived, 0 void.** Table, kill
counts and what the probes found: [MUTATIONS.md](MUTATIONS.md). They found one test
gap (the order of the division in the convection entries was not pinned; closed
with two assertions) and nothing wrong in the production logic; six probes did not
compile under `-Werror` and were reformulated.

## Adversarial review

40 questions from the brief and 9 of the reviewer's, each answered with a test or a
probe: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). **One production defect**,
found not by the review or the mutations but by the first qualification run's
shared-library preset: a struct whose member functions are defined in the library
lacked the DLL export annotation, so debug-shared-ext failed to link while every
static build passed. Fixed, and the whole qualification rerun on the corrected tree
(finding F7). No gate-blocking finding.

## Determinism

[DETERMINISM.md](DETERMINISM.md): same-preset repeatability, and the cross-preset
comparison measured on each preset's own binary.

## Known limitations

- A drilled hole's cylindrical wall cannot be a target: P16's `FaceRole` has no role
  for it. Inherited, recorded, not worked around.
- `TargetWithoutFacets` cannot be produced from a document, only through the
  integration seams. It is a defensive value.
- The currentness gate is a function the assembler must call; assembly does not
  exist yet, so nothing yet *forces* the call. P18-ASSEMBLY-001 must.
- A canonical `h = 0` is refused (see above); allowing it is the owner's decision.
- Nothing is persisted from preparation: the prepared boundary is derived, in
  memory, and does not survive a remesh by construction.
- Global assembly, the reduced solve, penalty enforcement, radiation, time-varying
  coefficients and thermal contact are not here and are not hinted at by any symbol.

## Regression

Nothing outside `src/thermal` and the test registrations changed, so the
predecessor suites are the regression, and they ran in every stage:

```text
unfiltered ctest, three presets    4448 / 4448 each   (4325 predecessor + 92 + 31)
repeat x5, release-ext, debug-ext  1495 / 1495 each   (1403 predecessor + 92)
P16 mesh and mapping, P17 structural elements, constraints and loads,
P18-ARCH/DATA/MAT/DOF/ELEM/LOAD    passed unchanged; the P17 structural-element
                                   suite alone is 29 cases, 4384 assertions
build                              0 warnings over 682 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               517 files, 0 violations
```

No generic boundary-mapping or constraint utility was changed, so P17-BC-001,
P17-LOAD-001 and P18-LOAD-001 needed no requalification (brief section 179); all
three are part of the unfiltered runs and passed.

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4448 of 4448 in each preset; repeat 1495 x 5 in two
THIS MILESTONE        92 ThermalBC cases, 4785 assertions; 31 compile-fail cases
WARNINGS              0 over 682 objects in each preset
STAGES                17, all exit 0; 07:54:43 -> 12:42:59, 4h48m16s; run 2 of 2
                      (run 1 failed the shared-library build on a missing export
                      annotation, a production defect, now fixed; preserved)
MUTATIONS             58 distinct, 58 killed, 0 survived, 0 void; 65 runs + a
                      5-run sample after the fix
ADVERSARIAL           40 + 9 questions, 7 findings, 1 production defect (fixed), 0 gate-blocking
LAYERING              517 files, 0 violations
CROSS-PRESET          bit fingerprints identical (hand Robin triangle 06de4e52c3ef700f,
                      real block 42982f4671b0e15b); 44 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and
                      after the last repeat; tests ce9f67bf, src 8a842d89,
                      include ecc31f14
CONTRACT              P18_THERMAL_BC_CONTRACT_FINGERPRINT 3c7e9500d53f4dd7d9d7e804830243db73f8392d
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-ASSEMBLY-001 -- Global Thermal Assembly. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.
