# P19-MESH-001 — re-audit of the recorded checklist, 2026-10-29

```text
RESULT:    NO VERDICT. This document claims no PASS and closes no gate. It is a
           static re-audit of the twenty-one items this milestone records as done,
           performed WITHOUT A BUILD, on an owner's instruction not to trust the
           existing marks.
TICKED:    nothing. TODO.md is unchanged by this audit: the twenty-one stay as
           they were and Determinism PASS / Regression PASS / Evidence recorded
           stay open.
FOUND:     no false mark among the twenty-one. Five are satisfied more narrowly
           than their wording suggests, and the suite has a geometry-oracle gap.
           Both are recorded below; neither is a defect in the gate.
MACHINE:   not the qualification machine. MSYS2 GCC 16.2.0, CMake 4.4.3. No
           WinLibs toolchain, no dependency prefix, no build root.
```

## Baseline

```text
BASELINE_HEAD        b474ac81aefa20155fcd46946bd559d18fe37e70
BASELINE_TREE        c44b0fc78c76cfdb3fabff1dad650b89dad88ec8
BASELINE_ORIGIN_MAIN b474ac81aefa20155fcd46946bd559d18fe37e70   (HEAD == origin/main)
working tree         3 tracked files modified, 6 untracked; nothing staged;
                     `git diff --check` clean
```

The twelve frozen fingerprints (the eight subtree hashes and the four milestone
fingerprints) were re-confirmed equal before and after this audit, which changed no
source or test file.

## Why no gate could be closed

The three open gates need a build. The project's own CMake refuses to configure on
this machine:

```text
cmake/BetterCADDependencies.cmake:85
  Open CASCADE Technology 7.9.3 found in C:/msys64/ucrt64/lib/cmake/opencascade;
  BetterCAD requires 8.0.x.
```

Qt 6.11.2 is absent and **Netgen 6.2.2604 is absent** -- the volume-meshing backend
every reference-mesh case in this suite runs on. **Zero tests were executed.** No
number in this document comes from a run; every statement is a reading of the source
or of recorded evidence, and is marked as such.

## Predecessors

Checked the recorded verdict *and* that each claimed implementation commit is an
ancestor of `HEAD`, rather than the TODO label alone.

| Milestone | Recorded verdict | Commits | Ancestor of HEAD |
| --- | --- | --- | --- |
| P16-QUAL-001 | PASS | — | — |
| P18-QUAL-001 | PASS 2026-10-27 | `b5dce07`, `4334bf8` | yes |
| P19-ARCH-001 | PASS 2026-10-27 | `42a3551`, `24a1496` | yes |
| P19-DATA-001 | PASS 2026-10-27 | `f08019a`, `c8b53f6` | yes |
| P19-FLUID-001 | PASS 2026-10-28 | `4e57b02`, `b1f151c` | yes |

No predecessor is unfinished, and this milestone repairs or bypasses none. The block
is the toolchain.

## The P16 / P19 relationship still matches ADR-063

P16's Tet4 mesh serves directly as the finite-volume mesh. No adapter, no second
mesh, no second geometry authority. `inspectCfdMesh` takes
`const meshing::Mesh&` and `const meshing::GeometryMeshMappingReport&`, so the gate
**cannot** mutate P16 coordinates, connectivity or node ordering; the const
signature is the proof, not a convention.

## The twenty-one, item by item

Production-enforced **and** exercised by a test that fails if the behaviour is
removed (sixteen items):

```text
Audit P16 suitability for CFD        P16_AUDIT.md, finding by finding
Define cell topology supported       Tet4; kTetFaces + kTetFaceOpposite
Define face topology                 sorted NodeId triple, keyOf()
Define boundary-face representation  claims == 1, covered by a P16 Triangle3
Define owner / neighbour relation    first claimant in ascending ElementId order
Define cell volumes                  meshing::signedVolume; NO abs(); refuses
                                     <= 0 and non-finite
Define outward-normal convention     dot(normal, apex - a) < 0
Define internal-face orientation     reversed(): exact, combinatorial, no tolerance
Define boundary patch mapping        one patch per CAD face of the map
Define named boundary mapping        through P16 only (FaceName -> boundaryFacetsOf)
Define geometry-source binding       GeometryRevision in the contract
Define mesh-source binding           MeshStamp; describes() by stamp, never counts
Reject zero / negative cell volume   NonPositiveCellVolume
Reject invalid face ownership        InvalidFaceOwnership (three cases)
Reject non-manifold volume topology  NonManifoldFace, claims >= 3
Reject unclosed fluid domain         UnclosedDomain
No nearest-face fallback             UnattributedBoundary refuses any gap
```

Satisfied as **recorded definitions exercised by test-side code**, with no
production computation (five items):

```text
Define cell centres                  ADR-063 §2. Computed in the TEST file
                                     (c[i] += p[i] / 4.0), not in src/cfd/.
Define face centres                  ADR-063 §2. Computed in the TEST file
                                     ((a + b + c) / 3.0), not in src/cfd/.
Define face area vectors             ADR-063 §2. S_f computed in the TEST file.
                                     Production takes a raw cross() for the
                                     outward test and NEVER scales it to A n:
                                     there is no face area and no |S_f| = A
                                     anywhere in src/cfd/CfdMesh.cpp.
Define mesh-quality metrics          ADR-063 §6. Non-orthogonality and skewness
required by CFD                      computed in the TEST file and reported by
                                     WARN (the F1 table), not in production.
```

**These marks were NOT changed, and the reasoning is on the record.** The item verb
is "Define"; ADR-063 fixes each formula normatively; this directory's README says
"definitions only, fixed in ADR-063"; and the computation belongs to P19-GEOM-001 by
that ADR, so implementing it here would widen the milestone. Unticking would assert
the definitions do not exist, which is false.

**But the wording oversells what exists.** A reader of `TODO.md` would reasonably
take "Define face area vectors" to mean production computes `S_f`, and it does not.
Re-wording these five, or moving them to P19-GEOM-001, is an owner decision and is
not taken here.

## Adversarial questions answerable without a build

Every one of these was checked by reading the source, and every one is clean.

```text
unordered_map / unordered_set in cfd    NONE in src/cfd, include/bettercad/cfd or
                                        tests/cfd. Faces live in a std::map keyed by
                                        sorted node handles, so no published order
                                        can follow hash iteration.
abs(volume) anywhere in the gate        none
nearest-face fallback                   "nearest" occurs only in comments stating
                                        what is forbidden
source mutation                         impossible: const& throughout
pointer / address dependence            none; owner is a cell's position in the
                                        mesh's own order
max-ID-sized allocation                 none: CellSets is sized by the cell COUNT,
                                        faces and coverage are std::map
union-find determinism                  a union always keeps the LOWER root, so the
                                        component count cannot depend on union order
4*Ncells == 2*Ninternal + Nboundary     asserted (CfdMeshTests.cpp:278)
boundaryFaces == boundaryTriangles      asserted (:276, :277)
manual tiny-topology oracle             the hand-built two-cell mesh asserts an
                                        explicit census {2, 1, 6, 0, 6, 1} (:473)
layering                                cfd 42 -> features 20, meshing 40, core 0.
                                        No GUI, CLI, thermal (45) or structural (50).
```

One difference from the brief worth recording, which is **not** a defect. The brief
asks for `S_f . (C_f - C_c) > 0` on a boundary face; production instead checks
`dot(normal, apex - a) < 0`. These are exactly equivalent: for a tetrahedron
`C_f - C_c = (C_f - apex) / 4`, and `normal` is perpendicular to the face plane, so
the two expressions differ only by the positive factor 4. The production form needs
no centroid, which is why it can exist while the centroid does not.

## The coverage gap this audit found

The suite is a **topology and refusal** suite, and a thorough one. It is not a
geometry-metric suite, and the brief's geometric oracles have no counterpart in it:

```text
rigid-translation test                  none in tests/cfd/
proper-rigid-rotation test              none
uniform-scaling test                    none
mm-contamination / unit test            none: no Approx, WithinAbs or WithinRel
                                        appears anywhere in CfdMeshTests.cpp, so no
                                        area or volume NUMBER is asserted against an
                                        analytical oracle
unit-Tet oracle (V = 1/6, C_c =         none
  (0.25, 0.25, 0.25), area sqrt(3)/2)
skew-Tet oracle                         none
large-mesh smoke                        none
sparse-ID memory smoke                  none
```

This gap and the five definition-only items are **the same fact seen twice**: there
is no production centroid, face area or area vector for an analytical oracle to
check. The gap therefore belongs with P19-GEOM-001, which owns the computation, and
closing it here would take that milestone's scope.

Partial mitigation already in the suite, by observation rather than assertion:
RM-MESH-06 supplies the same block at the origin and rotated-and-moved, and the
recorded F1 table shows identical quality figures for both (66.00 / 47.04 / 0.8206).
That is consistent with rigid invariance; it is not a test of it, because nothing
asserts the equality.

## What this audit did not and could not do

```text
determinism repeats                     BLOCKED, no build
three-preset qualification              BLOCKED, no build
mutation testing                        BLOCKED, no build
full regression                         BLOCKED, no build
warning audit                           BLOCKED, no build
cross-preset topology comparison        BLOCKED, no build
candidate-freeze verification           the twelve fingerprints were re-confirmed,
                                        but a freeze is only an identity, not a pass
```

## Result

```text
RESULT:   NO VERDICT. P19-MESH-001 remains BLOCKED, as FREEZE.md records.
AUDIT:    twenty-one recorded items re-read against src/cfd/CfdMesh.cpp,
          include/bettercad/cfd/CfdMesh.hpp and tests/cfd/CfdMeshTests.cpp;
          0 false marks; 5 narrower than their wording; 1 coverage gap, owned by
          P19-GEOM-001; 0 defects found in the gate.
TICKED:   nothing. TODO.md unchanged.
TESTS:    0 executed, on any preset.
NEXT:     the clean re-run on the qualification machine. P19-TOPO-001 is NOT
          AUTHORIZED and was not started.
```

## Revision

First issue, 2026-10-29: the static re-audit above, performed on an owner's
instruction to distrust the recorded marks. No gate closed, no mark changed, no
source or test file touched.
