# P18-LOAD-001 — Thermal Loads

```text
MILESTONE:  P18-LOAD-001
SUBJECT:    a surface heat flux q'' [W/m^2] and a volumetric generation q'''
            [W/m^3], resolved through P16's CAD-to-mesh mapping and integrated
            to the nodal heat input Q [W] a later assembly will use as the
            right-hand side
RESULT:     PASSED 2026-10-18, qualified in three presets, 4325 of 4325 each
```

## Baseline

```text
HEAD                 f10f5f07694bc3c5cec001a485eaaf9f1f5eee30   (P18-ELEM-001's record commit)
origin/main          f10f5f07...  (the owner's push of the ten P18 commits, verified)
predecessor suite    4240 of 4240, three presets (P18-ELEM-001)
this milestone       4325 tests: 4240 + 66 ThermalLoad cases + 19 compile-fail cases
```

## What the audit decided before any code was written

Full table in [LOAD_MODEL.md](LOAD_MODEL.md). The decisions that shaped the work:

- **The canonical load already existed.** P18-DATA-001 froze `ThermalLoad` as
  either a `SurfaceHeatFluxLoad {FaceName, HeatFlux}` or a
  `VolumetricHeatSourceLoad {VolumetricHeatSource}`, with *positive = heat into
  the body*. This milestone adds nothing to the persisted model. A canonical load
  holds no facet, node, element or unknown.
- **`FaceName` is the GeometryReference.** It is the one canonical face reference
  in the tree (P12-STREF-001), and P16's `boundaryFacetsOf` is the only thing
  that resolves it.
- **P17's face-target triage is structural-only,** so thermal gets its own copy of
  the same three checks (`ThermalTarget`), which P18-BC-001 will share. Nothing in
  P16 or P17 was changed, so no predecessor needed requalifying.
- **Provenance is the mesh, not the analysis revision.** A prepared load is stale
  when the body, the mesh stamp (and node count), the geometry revision or the
  load list changes. It is *not* stale when the material, the solver settings or
  a boundary condition changes: a prescribed heat input depends on none of them.
  The reasoning, and why the analysis revision would be wrong, is in
  [REMESH_CURRENTNESS.md](REMESH_CURRENTNESS.md).

## What this milestone adds

```text
include/bettercad/thermal/ThermalTarget.hpp       resolve a FaceName against the mapping, three named problems
include/bettercad/thermal/ThermalLoadVector.hpp   the integration seams, PreparedThermalLoads, currentness
src/thermal/ThermalTarget.cpp                     one pass over P16's answer; no geometry classification
src/thermal/ThermalLoadVector.cpp                 the two integrals, the superposition, the provenance
tests/thermal/ThermalLoadTests.cpp                66 cases
tests/compile_fail/ThermalLoadMisuse.cpp          19 cases
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or
`apps/` changed. The thermal layer includes only core, features and meshing
(ADR-050); the layering checker reports **513 files, 0 violations**.

## The frozen formulation

| | |
| --- | --- |
| surface | `Q_i = q'' A / 3` at each corner of each boundary facet, `A` the unsigned P16 triangle area in m² |
| volume | `Q_i = q''' V / 4` at each corner of each tetrahedron of the body, `V` the signed volume of a validated element in m³ |
| accumulation | `Q[dof] += share`, by `ThermalDofMap` ordinal, in the order of the loads then of the entities |
| conservation | `sum Q = q'' sum A_f` and `sum Q = q''' sum V_e` to a few ulps; recorded per load and in total |
| sign | positive is heat *into* the body; a negative value extracts; nothing takes an absolute value |
| units | W/m², W/m³ in; W out. The shares are formed as `q'' (A/3)` and `q''' (V/4)` so a running-sum overflow is reachable and caught |

[SIGN_CONVENTION.md](SIGN_CONVENTION.md), [UNITS.md](UNITS.md) (with the
tolerance table, one class per row), [SURFACE_HEAT_FLUX.md](SURFACE_HEAT_FLUX.md),
[VOLUMETRIC_GENERATION.md](VOLUMETRIC_GENERATION.md),
[SUPERPOSITION.md](SUPERPOSITION.md), [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)
(with the source audits), [REMESH_CURRENTNESS.md](REMESH_CURRENTNESS.md).

Failure is classified by twelve named problems, all tested, and is **atomic**:
one bad load refuses the whole set and nothing is published.

## Tests

66 cases, 21 588 assertions, in `tests/thermal/ThermalLoadTests.cpp`, built from
two kinds of fixture. **Hand meshes** go through the integration seams with
expected values computed *in the test* from the coordinates, with no production
helper as oracle. **Real meshes** go through the whole chain (document, extrude,
P16 mesh, mapping, `ThermalModel`, `ThermalDofMap`): a 40 × 30 × 20 mm block and
a 6 mm radius cylinder with a coarse and a fine control whose boundary meshes
genuinely differ.

| Group | Cases | What it settles |
| --- | --- | --- |
| definition | 5 | the canonical load holds a `FaceName` and a quantity; units; sign |
| surface | 8 | triangle oracles, scaling, shared nodes, triangulation, winding, conservation |
| volume | 8 | unit and general tetrahedron, scaling, shared nodes, block conservation, material independence |
| mapping | 7 | resolution through P16, unresolved, malformed, empty, no nearest face, all-or-nothing |
| remesh and currentness | 12 | same counts, same ids, different body, edits, material/solver/BC do not invalidate |
| sparse ids | 3 | node ids 7, 104, 9001, 500000 and 4 294 967 294 |
| superposition | 5 | surface, volume, mixed, signed cancellation, load order |
| invalid input | 12 | non-finite, zero area, inverted tet, overflow, repeated entities, foreign numbering |
| purity | 2 | canonical and derived state untouched; a failure publishes nothing |
| determinism | 3 | 50 repeats, diagnostics, a bit fingerprint |
| scope | 1 | no convection or temperature in the prepared type |
| compile-fail | 19 | `q''` is not a `double`, a generation is not a heat flux, a node list is not a facet list, the prepared type cannot be built or edited, the loads carry no mesh identity |

Measured values the suite prints (a few of them): a 3 m² triangle at 12 W/m²
gives 12 W per corner and 36 W; a unit tetrahedron at 24 W/m³ gives 1 W per
corner and 4 W; the block's end cap (two facets, four of nine nodes) carries
exactly 0.6 W at 500 W/m²; the block (twelve tetrahedra, 2.4e-5 m³) carries 60 W
at 2.5e6 W/m³; the cylinder's cap area is −0.507 % against the CAD value on the
coarse mesh and −0.032 % on the fine one, and the two totals differ by 0.475 %,
which is the discretisation of the mesh and nothing else.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries nine `ThermalLoad` filters and one
compile-fail filter, each with a minimum equal to the observed count (66, 10, 10,
7, 12, 3, 5, 12, 3 and 19), covering the brief's eight groups: surface, volume,
mapping, remesh/currentness, sparse-ID, superposition, invalid-input and
determinism. `verify-harness.cmd` (11855b8f) passed beforehand and shows a
filter that matches nothing is counted as a failed stage.

## Mutation probes

46 distinct mutations in 63 runs: **45 killed, 1 retired as dead code, 0
survived, 0 void.** Table, kill counts and what the probes found: see
[MUTATIONS.md](MUTATIONS.md). They found two defects in the first version of the
production code: a finiteness scan that could never fire (deleted), and an
overflow gate that no input could reach because of the order of a multiplication
and a division (reordered), and four gaps in the tests (filled).

## Adversarial review

34 questions from the brief and 10 of the reviewer's own, each answered with a
test or a probe: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). No gate-blocking
finding.

## Determinism

[DETERMINISM.md](DETERMINISM.md): same-preset repeatability, and the cross-preset
comparison measured on each preset's own binary.

## Known limitations

- A drilled hole's cylindrical wall cannot be a load target: P16's `FaceRole` has
  no role for it. Inherited, recorded, not worked around.
- `TargetWithoutFacets` and `BodyWithoutElements` cannot be produced from a
  document, only through the integration seams. They are defensive values.
- Internal faces cannot be loaded: P16 attributes boundary triangles to CAD faces
  only.
- The currentness gate is a function the assembly must call; assembly does not
  exist yet, so nothing yet *forces* the call. P18-ASSEMBLY-001 must.
- Nothing is persisted from preparation: the prepared loads are derived, in
  memory, and do not survive a remesh by construction.
- Convection, prescribed temperature, assembly, solve and transient terms are not
  here and are not hinted at by any symbol.

## Regression

Nothing outside `src/thermal` and the test registrations changed, so the
predecessor suites are the regression, and they ran in every stage:

```text
unfiltered ctest, three presets    4325 / 4325 each   (4240 predecessor + 66 + 19)
repeat x5, release-ext, debug-ext  1403 / 1403 each   (1337 predecessor + 66)
P16 mesh and mapping, P17 structural elements and loads, P18-ARCH/DATA/MAT/DOF/ELEM
                                   passed unchanged; the P17 structural-element
                                   suite alone is 29 cases, 4384 assertions
build                              0 warnings over 677 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               513 files, 0 violations
```

No generic boundary-mapping utility was changed, so P17-LOAD-001 needed no
requalification (brief section 174); it is part of the unfiltered runs and
passed.

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4325 of 4325 in each preset; repeat 1403 x 5 in two
THIS MILESTONE        66 ThermalLoad cases, 21 588 assertions; 19 compile-fail cases
WARNINGS              0 over 677 objects in each preset
STAGES                17, all exit 0; 17:51:35 -> 22:13:03, 4h21m28s; one run
MUTATIONS             46 distinct, 45 killed, 1 retired as dead code, 0 survived, 0 void
ADVERSARIAL           34 + 10 questions, 9 findings, 0 gate-blocking
LAYERING              513 files, 0 violations
CROSS-PRESET          bit fingerprints identical (hand cases 3063bb8f8518f832,
                      real block 9a0ad0769a0a552f); 42 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and
                      after the last repeat; tests 4bfc1876, src f1e63c1a,
                      include 1822fe06
CONTRACT              P18_THERMAL_LOAD_CONTRACT_FINGERPRINT 96e122c382463febddb85ff64f800e7be1d61777
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-BC-001 -- Thermal Boundary Conditions. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.
