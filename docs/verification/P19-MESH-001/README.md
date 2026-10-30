# P19-MESH-001 — CFD Volume / Surface Mesh Contract

```text
STATUS:    PASS. Qualified 2026-10-31 by attempt 2, run on the replacement
           machine: 17 of 17 stages exit 0, 5939 / 5939 tests in each of the
           three presets, 2803 x 5 in two, 0 warnings over 755 objects, and the
           twelve frozen identities unchanged before and after. Attempt 1
           (2026-10-28) FAILED for an environmental reason (the previous
           machine's temp volume was full: every build died, no suite ran) and
           is kept recorded, not erased. Details: FREEZE.md; the machine change
           itself: ENVIRONMENT-MIGRATION.md.
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
PASS. Attempt 2, the re-run on the replacement machine,
Fri 30/10/2026 23:04:51 -> Sat 31/10/2026 01:50:59 (2h46m08s):
17 of 17 stages exit 0, qualify.cmd exit 0, "0 stage(s) failed".

preset              configure  clean      build           ctest
debug-ext           exit 0     779 files  780/780  8m20s  5939/5939  1993.73s
release-ext         exit 0       0 files  780/780  9m43s  5939/5939  1922.85s
debug-shared-ext    exit 0       0 files  780/780  7m51s  5939/5939  2234.29s

repeat release-ext  2803 selected, --repeat until-fail:5, 14015 Passed  962.53s
repeat debug-ext    2803 selected, --repeat until-fail:5, 14015 Passed 1254.92s

0 warnings and 0 errors in all three build logs, over 755 objects (cfd: 6).
The no-op rebuild is 1 edge per preset: the git revision stamp target, which
always runs. debug-ext's clean removed 779 files and the other two removed 0,
because debug-ext's tree had been populated by the environment-qualification
build that preceded the run; all three then compiled 780 of 780 edges, so every
preset did build from clean.

Genuine ctest failure markers -- ***Failed, ***Exception, ***Timeout,
***Skipped, ***Not Run -- are 0 in all five logs, and the per-test Passed
markers were counted exactly: 5939 in each full log, 14015 (2803 x 5) in each
repeat log.

The 11 GUI-dependent tests are present in all three presets. That is what makes
5939 rather than 5928 the proof that the environment is whole: a build without
Qt silently omits exactly those eleven and still passes everything it selects.
cli.new.unicode-path passes in all three, so qualify.cmd's chcp 65001 is in
force. All 20 unit.CfdMesh_* cases are inside the repeat selection and appear
100 times (20 x 5) in each repeat log, so the determinism stage exercised this
milestone's own code rather than passing a selection that excluded it.

Harness stderr is 270 lines, all 270 of them git line-ending notices from the
harness's own tree captures. The tree captures taken before the first build and
after the last test run are identical.
```

[FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    5939 / 5939 by the harness in each of debug-ext, release-ext and
          debug-shared-ext; 2803 x 5 in release-ext and debug-ext with 14015
          counted per-test Passed markers per repeat log; 0 failures, 0
          exceptions, 0 timeouts, 0 skips, 0 not-run
VALIDATION: on each preset's own executable, the CFD data, boundary, fluid and
          mesh cases: 3727 assertions in 97 test cases, identical on all three;
          the golden fingerprint cfd-definition-v1:20707079b32e4964 identical on
          all three; the CfdMesh_* census and measured quality byte-identical on
          all three (six evidence files reduce to two sha256 values, one per
          invocation type). Architecture check 564 files / 0 violations, and the
          target-dependency check holds: cfd links neither thermal nor
          structural. The F1 quality rows are unchanged from ADR-063's.
TREE:     the twelve frozen identities -- the eight subtree hashes and the four
          milestone fingerprints -- identical before the first build, after the
          last test and after all post-run verification; qualify.cmd d313a640,
          as in all fifty-four milestones
EVIDENCE: this directory: configure-*.log, clean-*.log, build-*.log,
          rebuild-*.log, ctest-*.log, ctest-repeat-*.log, qualification-times.txt,
          run-qualification.out, run-qualification.err, postrun-summary.txt,
          cfdmesh-*.txt, cfdmesh-census-*.txt
ADR:      ADR-063
ENVIRONMENT: the previous qualification machine was permanently replaced; the new
          one was qualified in its own right before the gate was run.
          ENVIRONMENT-MIGRATION.md
NEXT:     P19-TOPO-001 is NOT AUTHORIZED
```

## Revision

First issue, 2026-10-28. Second issue, 2026-10-29: the review of attempt 1,
its cause recorded, the placeholders replaced by the true state; the Regression
and Result sections kept a refusal marker so commit-mesh.ps1 would refuse to
commit until a passing re-run rewrote them. It has now been rewritten by the
passing attempt 2 (seventh issue below).
Third issue, 2026-10-29: the recovery steps possible without the qualification
machine (fingerprint re-confirmed, harness comment corrected, storage analysed)
are recorded in FREEZE.md; the re-run itself remains BLOCKED there.
Fourth issue, 2026-10-29: the free-space preflight added in front of the harness
(FREEZE.md, hand-tested in twelve cases on 2026-10-29, including the two that prove a refusal stops a parent batch before its next line), and the static re-audit of the twenty-one
recorded items in RE_AUDIT.md -- no verdict, no gate closed, no mark changed.

Fifth issue, 2026-10-30: the frozen candidate built and its whole suite run twice on the second machine, recorded in FREEZE.md and in `local-run-2026-10-30/` -- 5928 / 5928 under code page 65001, one preset, outside the harness. Supporting evidence only: no gate closed, no mark changed, no source or test file touched, and the eight subtree hashes and four milestone fingerprints re-confirmed unchanged.

Sixth issue, 2026-10-30: the second-machine run extended to all three qualification presets and both determinism repeats, recorded in FREEZE.md and in `local-run-2026-10-30/` -- 5928 / 5928 per preset, 2793 x 5 with 13965 Passed lines per repeat stage, all exit 0, and the eight F1 quality rows identical across optimisation levels and equal to ADR-063's. This supersedes the "one preset" scope of the fifth issue above. Supporting evidence only: no gate closed, no mark changed, no source or test file touched, and the eight subtree hashes and four milestone fingerprints re-confirmed unchanged. `Determinism PASS`, `Regression PASS` and `Evidence recorded` stay open, because the gate is three presets through `qualify.cmd` at a counted 2803 on the authorised toolchain.

Seventh issue, 2026-10-31: attempt 2 PASSED and this record is now a pass. The
previous qualification machine was permanently replaced, so the environment was
reproduced and qualified in its own right first -- same WinGet WinLibs GCC 16.1.0
bundle (which also supplies CMake 4.4.1 and Ninja 1.13.2), and Qt 6.11.2, OCCT
8.0.1, Netgen 6.2.2604 and zlib 1.3.1 built by the repository's own hash-verified
superbuild with that same compiler; no MSVC-built Qt is involved anywhere
(ENVIRONMENT-MIGRATION.md). The gate then ran clean: 17 of 17 stages exit 0, 5939
per preset, 2803 x 5 in two, 0 warnings, twelve identities unchanged.

One gate did fail on the way, and the fix was to the checker, not the product.
postrun-mesh.ps1 compares the CfdMesh_* census and quality text across presets,
and it ran the test binary without pinning a seed. That binary's own --help says
"--order <decl|lex|rand>  test case order (defaults to rand)", so a direct
invocation shuffles the cases on a per-run seed and the stdout block order is
random; the CfdMesh_* filter emits two reporting blocks, so three independently
shuffled runs agree only 1 time in 4. It duly failed, with debug-ext and
release-ext agreeing and debug-shared-ext not. Every measured value was in fact
identical -- all 16 RM-MESH census and quality lines matched once sorted, on
repeated runs of the same binary and across presets -- and running the same
binary twice reproduced the disagreement, which ruled out the tidier hypothesis
that static-versus-shared initialiser order was responsible. --rng-seed 1, the
convention the P12 collect-evidence.cmd scripts already use for direct-binary
evidence runs, makes all three presets byte-identical and the check deterministic;
it makes the check STRICTER, since it can no longer pass by luck. The comparison
itself was not touched and was not made order-insensitive. The failing summary is
kept outside the repository at C:\bettercad\attempt-1-evidence\ as
postrun-summary-FAILED-unpinned-seed.txt: a gate that failed is evidence too.
postrun-mesh.ps1 lives under docs/verification/, in neither the eight
fingerprinted paths nor the four milestone fingerprints, so the fix did not void
the freeze -- confirmed by a final freeze capture whose identity portion is
byte-identical to the pre-run one.
