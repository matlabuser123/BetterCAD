# P16-VOL-001 — 3D Tetrahedral Volume Mesh

```text
STATUS:   PASS
TASK:     P16-VOL-001 -- 3D tetrahedral volume mesh (Tet4)
PHASE:    P16 -- Meshing
DATE:     2026-10-02
```

BetterCAD generates a validated Tet4 volume mesh from authoritative CAD
geometry, through the boundaries `P16-GEOM-001` and `P16-SURF-001` already
qualified, using the Netgen backend `INFRA-NETGEN-001` qualified.

```text
authoritative geometry -> prepared -> engineering surface -> tetrahedra -> VolumeMesh
```

**A `VolumeMesh` cannot be fabricated.** Its constructor is private and
`generateVolumeMesh` is its only friend, so possessing one is the evidence that
the mesh passed validation, conformity and volume recovery. Three compile-fail
cases prove it. This is ADR-030's mechanism — "enforced by the type system, not
by documentation" — applied.

## Documents

```text
ARCHITECTURE.md       the seam, the domain type, and the decisions taken
NETGEN_PIPELINE.md    what crosses the backend boundary, and why NG_OK is not
                      evidence
REFERENCE_CASES.md    the fixtures, what each one is FOR, what was measured
VALIDATION.md         what is checked against what, and every tolerance's reason
ADVERSARIAL_REVIEW.md 4 defects found by the review, 4 limitations carried
qualification/        the logs
HISTORY_WITHDRAWN_BLOCK_REPORT.md
                      this milestone's first two issues, kept in full: a BLOCKED
                      report and its withdrawal. See History, below.
```

## Baseline

```text
branch        main
HEAD at start eaf7da4  BetterCAD: qualify Netgen as the volume-meshing backend
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
backend       Netgen 6.2.2604, reported by the library itself
OCCT          8.0.1
```

Prerequisites, all verified before anything was written:

```text
P16-ARCH-001      ADR-030 to ADR-033            evidence at 9964f88
P16-DATA-001      Tet4, identity, validation    evidence at 20b04b9
P16-GEOM-001      geometry/currency boundary    evidence at 9c755e8
P16-SURF-001      engineering surface mesh      evidence at 4005815
INFRA-NETGEN-001  the backend, qualified        evidence at eaf7da4
```

## How much was already there

The largest fact about this milestone, and the reason the production diff is
small: **P16-DATA-001 had already qualified the entire Tet4 data model.** The
element, `signedVolume` with exactly the formula the brief specifies, the
positive-volume convention, and six of the seven element checks were in place
and tested.

```text
asked for                        found, reused unchanged
-------------------------------  ------------------------------------------
Tet4 with four node references    meshing::Tetrahedron
mesh-local NodeId / ElementId      MeshIds.hpp + compile-fail proof
V = 1/6 det(p2-p1, p3-p1, p4-p1)   meshing::signedVolume
positive volume is valid           the convention; no abs() anywhere
reject negative / zero volume      Inverted- / DegenerateTetrahedron
reject missing node reference      MissingNodeReference, and the builder
                                   refuses it at add time
reject repeated node in element    RepeatedNodeReference, likewise
```

Writing a parallel `VolumeMesh`/`Tet4Element` pair beside these would have been
the duplicate-rule defect `P16-GEOM-001` was bitten by, where two definitions of
"is this body eligible" had drifted. One definition of "is this tetrahedron
valid" serves every producer.

**The one gap was duplicate detection**, which this milestone adds to
`validate()` as `MeshIssueKind::DuplicateTetrahedron`. VALIDATION.md explains
why there rather than in `P16-QUALITY-001`.

## What this milestone added

```text
include/bettercad/meshing/VolumeBackend.hpp   the ADR-033 seam, extended
include/bettercad/meshing/VolumeMesh.hpp      VolumeMesh, controls, conformity
src/meshing/VolumeMesh.cpp                     the mesher
src/meshing/netgen/NetgenBackend.cpp           nglib, in the only file allowed
src/meshing/NoVolumeBackend.cpp                the no-backend half
include/bettercad/meshing/MeshValidation.hpp   + DuplicateTetrahedron
src/meshing/MeshValidation.cpp                 its detector
tests/meshing/NetgenVolumeBackendTests.cpp     the seam
tests/meshing/TetValidationTests.cpp           the new validation
tests/meshing/VolumeMeshTests.cpp              end to end from CAD
tests/compile_fail/VolumeMeshMisuse.cpp        the type-system claim
```

## Results

```text
box 20 x 30 x 40 mm     9 nodes, 12 tetrahedra, 12 boundary triangles
                        tetrahedral volume 2.4e-05 m^3
                        boundary volume    2.4e-05 m^3
                        CAD volume         2.4e-05 m^3   (= 20*30*40, by hand)
                        conformity: 0 unmatched in BOTH directions
```

Full set, including the cylinder's convergence and the tube's void preservation:
REFERENCE_CASES.md.

## Regression

Full qualification, `qualification/qualify.cmd`, harness carried unchanged from
`P15-QUAL-001` (its own regression was re-run and passed). Each preset is
configured, has **every** build output removed, is rebuilt with warnings as
errors, and only then runs CTest — unfiltered.

```text
preset             build   warnings  ctest                    time
debug-ext          exit 0  0         2983/2983 passed (100%)  726.46 s
release-ext        exit 0  0         2983/2983 passed (100%)  1029.53 s
debug-shared-ext   exit 0  0         2983/2983 passed (100%)  981.34 s

repeat release-ext   178 tests selected, x5, exit 0
repeat debug-ext     178 tests selected, x5, exit 0

stages failed: 0          qualify.cmd exit 0
started 11:52:16   finished 14:05:19   2026-10-02
```

```text
test count   2937 (INFRA-NETGEN-001) -> 2983, exactly +46:
             43 x unit.Vol* / unit.Tet* / unit.Duplicate* (volume mesher,
                  backend seam, new validation)
              3 x compile_fail.volumemesh.*
executions   2983 x 3 presets + 178 x 5 x 2 repeat presets = 10 729
```

The repeat set of 178 is the blast radius rather than the new tests alone: the
whole meshing module, every surface-mesh test, all 10 architecture tests and
both mesh compile-fail groups, because this change touched `src/meshing/`,
`validate()`'s issue enum and the CMake that configures them.

`debug-shared-ext` matters more than usual here: `VolumeMesh` is an exported
class with a private constructor and a friend declaration, and only the shared
preset exercises that across a DLL boundary.

### Qualified tree == committed tree

The harness fingerprints the source before the first build and after the last
test, from a scratch index over the working tree:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           2af9f93c3eb0516a66d3ce43fa9f8a33dc1f1d1a
src               13b2b10c9593eb97a6688e5e32779b42468fd14b
tests             e6c9a79fad40d3e7576f5a4be17c556a94c5f996
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Identical before and after, and identical to the fingerprint recorded
independently at the freeze. These are the trees that were committed.

**A first attempt at this qualification was stopped**, 30 minutes in, by the
agent harness's background-task time limit rather than by any failure — it had
completed debug-ext's configure, clean and build. That run is void and is not
reported: a clean-rebuild qualification is all-or-nothing, and a partial one
proves nothing. The run above is a complete fresh run of the same frozen tree,
launched as a detached process so the limit could not reach it.

## The checklist

```text
[x] Generate tetrahedral mesh from valid closed solid
[x] Every element references valid nodes
[x] Every tetrahedron has positive qualified volume
[x] No inverted tetrahedra
[x] No zero-volume tetrahedra
[x] No duplicate tetrahedra
[x] Boundary conforms to engineering surface
[x] Internal voids remain void
[x] Mesh occupies solid volume
[x] Nodes remain inside/on valid geometry within tolerance
[x] Element volumes approximately recover CAD volume
[x] Validate disconnected solid policy        REFUSED, explicitly and tested
[x] Validate transformed body
[x] Validate small feature behaviour          SUPPORTED at 1 mm and 0.4 mm
[x] Backend failures propagate explicitly
[x] Determinism measured                      within a preset; see limitations
[x] Adversarial review PASS                   4 defects found and fixed
[x] Regression PASS
[x] Evidence recorded
```

## Independent validation

Every acceptance check is against something computed independently of the code
under test. Detail and tolerance justification: VALIDATION.md.

```text
the boundary's enclosed volume   an exact identity: the tetrahedra tile the
                                 polyhedron the surface bounds. The only
                                 acceptance gate on volume.
the CAD volume                   recorded and asserted in tests as a one-sided
                                 bound, because facets understate a curved body
hand-computed volumes            box 24000 mm^3; cylinder pi r^2 h; tube
                                 pi(Ro^2-Ri^2)h; tetrahedron 1/6
the boundary, from connectivity  faces used by exactly one tetrahedron. The
                                 backend's own surface report is never consulted
counted both ways                unmatched boundary faces AND unmatched surface
                                 triangles, because one direction cannot see a
                                 backend that refined the boundary
no absolute values               two equal tetrahedra, one inverted, must sum to
                                 ZERO
```

## Known limitations

```text
nothing forces a cached mesh to be re-checked
                      A VolumeMesh records its source and revision and
                      isStale(document, mesh) answers truthfully, but a holder
                      that never asks can read a mesh of geometry that has
                      moved. The request path refuses stale geometry, so this
                      bites only code that caches. P17's solver entry point
                      should take the document and feature, or re-check.

cross-preset determinism not asserted
                      Five runs agree exactly within a preset, on connectivity
                      and node positions. Across presets nothing compares,
                      because no artefact is exported yet. Waits for
                      P16-CLI-001 or P16-VIZ-001.

concurrency not exercised
                      Calls into the backend are serialised by a mutex because
                      nglib keeps global state. No test runs two threads.

multiple solids refused
                      ADR-032's one-region-per-solid remains the eventual
                      design. The refusal is explicit and tested.

no sphere fixture     Carried from P16-SURF-001: a degenerate pole edge is the
                      one geometry where exact-coordinate node unification is
                      untested. Still untested.

quality is not assessed
                      A thin tetrahedron is data-valid here, deliberately.
                      Slivers, aspect ratios and dihedral angles are
                      P16-QUALITY-001's.

no sanitizer coverage This MinGW ships no libasan/libubsan. None is claimed,
                      for Netgen or for this code.
```

## History

This milestone was reported twice before it was implemented, and both records
are kept rather than tidied away:

```text
First issue    2026-10-01   BLOCKED: "the approved backend does not build on
                            this toolchain".
Second issue   2026-10-01   WITHDRAWN. The block was a misdiagnosis -- Netgen
                            builds; the fault was a mismatched C++ runtime in
                            the investigating environment. INFRA-NETGEN-001
                            found the real cause and qualified the backend.
Third issue    2026-10-02   IMPLEMENTED. This document.
```

The first two are in HISTORY_WITHDRAWN_BLOCK_REPORT.md, with the superseded
passages marked in place. The reasoning that produced the false BLOCKED is
recorded there too, because it was confident and the failure mode is easy to
repeat.

## Result

```text
RESULT:   PASS
TODO:     19/19 ticked.
NEXT:     a scope decision. P16-SIZE-001, P16-QUALITY-001, P16-MAP-001 and
          P16-VIZ-001 are all now reachable; none is authorized by this
          document.
```
