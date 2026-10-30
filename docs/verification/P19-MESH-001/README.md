# P19-MESH-001 — CFD Volume / Surface Mesh Contract

```text
STATUS:    BLOCKED. Qualification attempt 1 (2026-10-28) FAILED for an
           environmental reason (the qualification machine's temp volume was
           full: every build died, no suite ran). Regression NOT passed; nothing
           qualified; to be re-run from clean. Details: FREEZE.md.
MILESTONE: P19-MESH-001, the fourth milestone of P19 — CFD Integration
SCOPE:     the mesh a CFD analysis runs on: P16's Tet4 mesh, qualified as the
           finite-volume mesh through a staged gate that proves the contract.
           No CellIndex, no metric in production, no threshold, no field, no solve.
```

## Baseline

```text
HEAD at start      b1f151c (P19-FLUID-001's record)
origin/main        b1f151c, verified
authorized         2026-10-28, the owner's "start P19-MESH-001", with the
                   checklist already recorded in TODO.md
```

## The critical question, answered

**Can P16's Tet4 mesh directly serve as the P19 finite-volume mesh? Yes, with no
adapter.**

- A cell is a P16 tetrahedron.
- A boundary face is a P16 boundary triangle.
- A patch is a CAD face from P16's `GeometryMeshMap`.
- Interior faces, owner and neighbour are derived from P16's connectivity and never
  persisted.

What P16 does not guarantee is proved by a gate, `requireCfdMesh(CfdModel) ->
CfdMeshContract`, staged after ADR-060's input boundary. Possession of the contract
is the proof, and the contract is bound to the mesh stamp. All eight meshable P16
reference bodies satisfy it, as does a drilled body with an unnamed wall.

The audit of P16, finding by finding: [P16_AUDIT.md](P16_AUDIT.md).

## What was decided (ADR-063)

```text
1  the P16 mesh serves directly; inspectCfdMesh + cfdMeshRefusal are the gate's own
   code on a plain mesh, so every refusal can be shown
2  definitions: cell = Tet4 (volume = signed, > 0; centre = vertex mean); face =
   sorted NodeId triple; owner = first claimant in ascending ElementId order;
   every face wound by its owner (normal owner -> neighbour, or out of the fluid);
   face centre = vertex mean; S_f = 1/2 (b - a) x (c - a)
3  patch = one CAD face of the map; names through P16 only (FaceName ->
   boundaryFacetsOf); an unnamed patch is counted, and P19-BC-001 must refuse it;
   NO nearest-face fallback
4  source binding: MeshStamp + GeometryRevision in the contract; describes() by stamp
5  nine refusals, in order: empty, non-positive volume, non-manifold face,
   inconsistent face orientation, disconnected cells, invalid face ownership,
   inward boundary face, unclosed domain, unattributed boundary
6  quality metrics defined (non-orthogonality, interior skewness, orthogonal
   distance, volume ratio); S . d > 0 proved and asserted; measured, not judged
7  the shared mesh seam ADR-060 proposed is NOT extracted (owner decision D2)
errata  ADR-058 (a Triangle3 has no owner) and ADR-032 (P16 refuses > 1 solid)
```

## Finding F1 — the reference meshes are poor finite-volume meshes

```text
model                   interior faces   theta max   theta mean   skewness max
RM-MESH-01 Block                   6        64.09        47.56         0.5376
RM-MESH-02 Cylinder              946        89.28        57.89         4.6077
RM-MESH-03 PlateWithHole         172        84.07        56.70         6.5099
RM-MESH-04 Tube                 1472        85.20        55.27         3.7138
RM-MESH-05 ThinPlate              18        88.81        66.48         0.8206
RM-MESH-06 TransformedBase        18        66.00        47.04         0.8206
RM-MESH-06 TransformedPlaced      18        66.00        47.04         0.8206
RM-MESH-07 LocalRefinement       528        75.58        31.14         1.3109
```

The topology passes everywhere, but the quality is poor. The non-orthogonal
correction is therefore mandatory in the solver. P19-VALID-001 may refuse some of
these meshes, and a benchmark will need finer P16 sizing or an owner-decided
FV-quality option in P16. It must never use a CFD-side remesh. No threshold is set
here.

## Tests

```text
tests/cfd/CfdMeshTests.cpp   20 cases  [cfd][mesh]
  every P16 reference mesh satisfies the contract; census == P16's faceIncidence
  an unnamed patch (drilled wall) is counted, not refused
  each of the nine refusals on a mesh built to fail it (vertex / edge / no-contact /
    three-piece disconnection; the fold; three ownership cases wound so that a
    fall-through would show; overflow to an infinite volume; a degenerate face)
  seven findings at once, in order, with counts; the refusal's exact text
  stamp binding across a remesh; unforgeable proof (compile-time); determinism
  the face table against P16's tetrahedralBoundary
  the quality measurement (F1), with S . d > 0 asserted on every interior face
tests/cli/ZeroMatchGuard.cmake  "unit\.CfdMesh_" 20 and each new case by name
```

## Adversarial review

```text
AUTHOR'S QUESTIONS     6 (A1-A6), each resolved by a fix and a test; A1 was the
                       3 - k opposite-vertex bug every P16 mesh caught
INDEPENDENT REVIEWER   first pass 0 blockers, 1 major (a shared face traversed the
                       same way by both cells passed: InconsistentFaceOrientation
                       added), 4 minor, 8 nits; re-verification: every fix correct,
                       0 new major/minor, 5 nits -- all fixed or recorded
MUTATION PROBES        31 of 31 killed, 0 survived, 0 equivalent (2 rewritten after
                       -Werror refused the first text; both runs kept)
SECOND READ 2026-10-29 the gate's code read line by line against ADR-063 on a second
                       machine; the face table, opposite-vertex table and the
                       reversed-winding rule re-derived independently in Python
                       (docs: FREEZE.md); 0 defects found. One text drift, the
                       comment in qualification/run-qualification.cmd saying
                       "eight named refusals" where the header and ADR-063 define
                       nine, corrected 2026-10-29 (comment only; hash in FREEZE.md).
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

A THIRD READ, 2026-10-29, re-audited all twenty-one recorded checklist items against
the source on an owner's instruction to distrust the marks: 0 false marks, 0 defects
in the gate, 5 items satisfied more narrowly than their wording suggests (cell
centres, face centres, face area vectors and the quality metrics are defined in
ADR-063 and computed in the TEST file, not in production), and one coverage gap owned
by P19-GEOM-001 (no rigid-transform, scaling, unit or analytical-geometry oracle,
because production holds no centroid, area or area vector to check). It closed no
gate and changed no mark; it ran no test, because this machine cannot build.
[RE_AUDIT.md](RE_AUDIT.md).

## Known limitations

```text
No CellIndex / FaceIndex (P19-TOPO-001), no production metric (P19-GEOM-001), no
  threshold (P19-VALID-001): definitions only, fixed in ADR-063.
F1: the reference meshes' FV quality is poor; judged by P19-VALID-001.
A vertex or edge pinch inside a face-connected mesh is accepted by decision; no
  fixture shows it (ADR-063 §5).
The gate relies on the CfdModel to pair the map with the mesh; the tests assert it.
The face table now has three copies (two file-local in P16, one public in cfd),
  pinned against P16's output.
The shared mesh seam is not extracted: owner decision D2.
This MinGW toolchain has no ASan/UBSan.
```

## Regression

```text
PLACEHOLDER until a re-run passes. Attempt 1, Wed 28/10/2026 19:47:09 -> 20:06:44:
5 of 17 stages FAILED, qualify.cmd exit 5. All three builds (debug-ext at object
127, release-ext at 330, debug-shared-ext at 181, of 780 steps) aborted with
"No space left on device" writing the compiler's temporary file under %TEMP%;
0 warnings before the abort; the three full ctest stages were skipped; the two
repeat stages ran against emptied build trees (147 of 352 "no such file or
directory") and measure nothing. Fingerprints before == after; the eight
subtree hashes were reproduced from the same tree on a second machine.
Not a code failure, and not a pass.
```

[FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PLACEHOLDER -- BLOCKED pending a clean re-run on the qualification
          machine (attempt 1 FAILED, environmental; see FREEZE.md)
TESTS:    0 / 0 executed by the harness in any preset (builds aborted); before the
          freeze, outside the harness, debug-ext only: 5938 / 5939 once, the one
          failure the code-page-sensitive cli.new.unicode-path, which passed when
          re-run under 65001; CFD suites 97 cases / 3727 assertions passed
TREE:     candidate fingerprint recorded in FREEZE.md; it is not a qualified tree
EVIDENCE: this directory, incomplete: no ctest-*.log, rebuild-*.log,
          postrun-summary.txt, cfdmesh-*.txt or freeze-mesh-*.txt yet
ADR:      ADR-063
NEXT:     re-run the qualification from clean after freeing the temp volume;
          P19-TOPO-001 is NOT AUTHORIZED
```

## Revision

First issue, 2026-10-28. Second issue, 2026-10-29: the review of attempt 1,
its cause recorded, the placeholders replaced by the true state; the Regression
and Result sections keep the word PLACEHOLDER so commit-mesh.ps1 refuses to
commit until a passing re-run rewrites them.
Third issue, 2026-10-29: the recovery steps possible without the qualification
machine (fingerprint re-confirmed, harness comment corrected, storage analysed)
are recorded in FREEZE.md; the re-run itself remains BLOCKED there.
Fourth issue, 2026-10-29: the free-space preflight added in front of the harness
(FREEZE.md, hand-tested in twelve cases on 2026-10-29, including the two that prove a refusal stops a parent batch before its next line), and the static re-audit of the twenty-one
recorded items in RE_AUDIT.md -- no verdict, no gate closed, no mark changed.

Fifth issue, 2026-10-30: the frozen candidate built and its whole suite run twice on the second machine, recorded in FREEZE.md and in `local-run-2026-10-30/` -- 5928 / 5928 under code page 65001, one preset, outside the harness. Supporting evidence only: no gate closed, no mark changed, no source or test file touched, and the eight subtree hashes and four milestone fingerprints re-confirmed unchanged.

Sixth issue, 2026-10-30: the second-machine run extended to all three qualification presets and both determinism repeats, recorded in FREEZE.md and in `local-run-2026-10-30/` -- 5928 / 5928 per preset, 2793 x 5 with 13965 Passed lines per repeat stage, all exit 0, and the eight F1 quality rows identical across optimisation levels and equal to ADR-063's. This supersedes the "one preset" scope of the fifth issue above. Supporting evidence only: no gate closed, no mark changed, no source or test file touched, and the eight subtree hashes and four milestone fingerprints re-confirmed unchanged. `Determinism PASS`, `Regression PASS` and `Evidence recorded` stay open, because the gate is three presets through `qualify.cmd` at a counted 2803 on the authorised toolchain.
