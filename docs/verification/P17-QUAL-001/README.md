# P17-QUAL-001 — Full P17 Qualification

```text
STATUS:   PASS
DATE:     2026-10-15
P17:      QUALIFIED
GATE:     all 17 P17 milestones PASS + CAD authority + P15 material canonical
          + P16 mesh derived + P17 results derived + stale geometry and stale
          mesh rejection + DOF model + Tet4 formulation + stiffness symmetry
          + rigid-body modes + loads + restraints + geometry mapping
          + assembly + solver + residual validation + displacement + strain
          + stress + von Mises + reactions + static equilibrium + explicit
          acceptance policy + visual inspection + undo/redo + persistence
          + CLI + CLI/core equivalence + reference models + analytical
          validation + patch test + three presets + determinism
          + cross-preset + adversarial review + 0 unexpected warnings
          + qualified tree == committed tree
```

```text
QUALIFICATION CANDIDATE   a99f0b6c332c4c122d495c55b99df598a083d86a + the working tree
SOURCE/TEST FINGERPRINT   apps a95876bc  include c9696d26  src f3c1245d
                          tests 90f93c52  examples a955cf65  cmake 5a382115
                          CMakeLists.txt 3ec3c10f  CMakePresets.json 3229f0f8
                          (recorded by the harness before the first build and
                          after the last test, identical; seven of the eight
                          identical to the P17-REFMOD-001 freeze)
QUALIFIED TREE METHOD     see FREEZE.md -- the eight-path fingerprint, because
                          docs/ is written after the run by design
```

```text
Netgen        6.2.2604          OCCT     8.0.1          Eigen   5.0.1 (MPL-2.0)
compiler      GCC 16.1.0, WinLibs POSIX UCRT
build         CMake 4.4.1 / Ninja 1.13.2; debug-ext, release-ext, debug-shared-ext
              BETTERCAD_BUILD_ROOT outside the source tree, on the hasib machine
harness       qualify.cmd byte-identical to P16-SIZE-001's and to every
              milestone's since, 31 in a row ending with this one
              git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
```

## Evidence

| Document | What it holds |
| --- | --- |
| [MILESTONE_AUDIT.md](MILESTONE_AUDIT.md) | the 17-row matrix: TODO boxes, evidence, RESULT, harness line, candidate, suite size, commit; the monotone suite growth; finding D1 |
| [ADR_AUDIT.md](ADR_AUDIT.md), [ADR_AUDIT_CLAUSES.md](ADR_AUDIT_CLAUSES.md) | 206 clauses of ADR-034..048 against the code: 195 implemented, 11 drifts amended, 0 invariant violations |
| [AUTHORITY_AUDIT.md](AUTHORITY_AUDIT.md) | the pipeline, the authority matrix as one executable statement, identity, stale-input protection |
| [CURRENTNESS_AUDIT.md](CURRENTNESS_AUDIT.md) | the provenance model, the truth table driven through the document, and the seven-edit sequence |
| [FORMULATION_AUDIT.md](FORMULATION_AUDIT.md) | Tet4, DOF, loads, restraints, assembly, solver, recovery, reactions, validation -- each failure mode to its test |
| [PERSISTENCE_AUDIT.md](PERSISTENCE_AUDIT.md) | persistence, commands, visualisation |
| [CLI_AUDIT.md](CLI_AUDIT.md) | no second implementation, CLI/core equivalence, refusals reach the exit status |
| [REFERENCE_MODELS.md](REFERENCE_MODELS.md) | the final matrix, PASS / N/A per property per model |
| [EQUILIBRIUM.md](EQUILIBRIUM.md) | the twelve documents through the production path, force and moment equilibrium, the model-change gate |
| [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md) | the oracles, the tolerance table re-measured, the convergence study, F1-F3, the no-loosening audit |
| [DETERMINISM.md](DETERMINISM.md) | 1138 x 5 in two presets, bit-exact fingerprints, and why |
| [CROSS_PRESET.md](CROSS_PRESET.md) | runner lines, suite fingerprints and CLI payloads identical across the three presets |
| [MUTATION.md](MUTATION.md) | 7 probes on the production tree, 7 killed, and the owning milestones' probes re-read |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | 30 + 14 questions, each with the attack, the test and the result; findings D1, A1-A11, T1 |
| [FINAL_REGRESSION.md](FINAL_REGRESSION.md) | the 17 stages, the warning audit, zero-match, the pre-freeze check |
| [FREEZE.md](FREEZE.md) | the fingerprint at three readings, the fresh binaries, the commit record |
| [qualification/](qualification/) | the harness, its logs, `prefreeze.txt`, and `postrun/` with every per-preset measurement |

## What this milestone changed

**No production code.** The candidate's eight-path fingerprint differs from
P17-REFMOD-001's in `tests` alone:

```text
apps      a95876bc  unchanged      examples  a955cf65  unchanged
include   c9696d26  unchanged      cmake     5a382115  unchanged
src       f3c1245d  unchanged      CMakeLists.txt / CMakePresets.json unchanged
tests     3568eb0e -> 90f93c52     MOVED
```

What moved: one new test file holding the three final cross-milestone gates,
its registration, and one more filter in the zero-match guard. Everything
else is documentation following evidence: eight ADR amendments,
`docs/architecture.md`'s layer table, two addenda in P17-VIZ-001, this
directory, and `TODO.md` / `ROADMAP.md` / `README.md`.

## Milestone audit

**17 of 17 PASS** -- 307 boxes ticked, an evidence directory with a recorded
`RESULT: PASS`, and each milestone's own `qualification-times.txt` ending in
`0 stage(s) failed`, where the harness's exit code *is* the number of failed
stages. The commit chain is unbroken from `a4290cf` to `a99f0b6`; every
candidate is its predecessor's commit. The suite grew monotonically from
3401 to 4006 across the phase and freezes at 4009 here.

One apparent gap was the evidence and not my extraction: P17-VIZ-001's
README still said `PARTIAL, 18 of 23` after the two runs that completed it.
The milestone is qualified; the README carries a dated addendum (D1).
[MILESTONE_AUDIT.md](MILESTONE_AUDIT.md).

## ADR audit

Fifteen ADRs, 206 clauses checked against the code by search and by reading
the cited line. **195 implemented; 11 findings, every one a sentence
overtaken by a later milestone, a count, or a mechanism described
differently from how it was built; 0 invariant violations.** Eight ADRs
carry a dated amendment and `docs/architecture.md`'s layer table was
brought back into agreement with the checker. The code was not changed.
[ADR_AUDIT.md](ADR_AUDIT.md).

## Authority, identity, currentness

Audited by searching for the **failure mode**, not the promise:

```text
Eigen outside src/structural (and the two older admissions)   0
NodeId / ElementId in src/io, include/bettercad/io              0 uses
DofIndex in core/Id.hpp                                         0
nearest-face / rebind logic in src/structural                   0
unordered containers, wall clock, randomness in structural      0
std::abs on a Jacobian determinant or a volume                  0
callers of solveStructuralAnalysis                              1 (the CLI's helper)
structural compile-fail files                                   9
```

**The authority matrix as one executable statement**: solving leaves every
one of the twelve committed reference documents byte-identical, with the
object count and the document revision unmoved -- ten solved, two refused.
**The currentness truth table as one sequence**: geometry, meshing intent,
material, load, restraint, undo, solver settings -- seven edits, seven
stale verdicts, eight solves, five of them on one mesh stamp, the first
result stale at the end. **A failed regeneration**: refused at the Inputs
stage with nothing measured past it, and recovery at the original depth
gives the same fingerprint bit for bit.
[AUTHORITY_AUDIT.md](AUTHORITY_AUDIT.md), [CURRENTNESS_AUDIT.md](CURRENTNESS_AUDIT.md).

## Formulation

Every failure mode the brief names traced to the line and to the test that
catches it, and seven of them to a mutation run at this gate:

```text
duplicate sparse entries overwrite (+= -> =)       KILLED, 11 cases
pivot gate removed                                 KILLED, 9
residual gate removed                              KILLED, 1 -- recorded as thin
inverted element admitted                          KILLED, 3
material edit no longer stales                     KILLED, 3
CLI exit 0 on a singular solve                     KILLED, 4 fresh-process tests
reaction sign flipped                              KILLED, 21
```

[FORMULATION_AUDIT.md](FORMULATION_AUDIT.md), [MUTATION.md](MUTATION.md).

## Reference models and analytical validation

```text
expected 10    executed 10    documents 12    solved 10    refused 2
```

Re-run on the frozen candidate's own binaries in all three presets: the
runner's 164 non-timing lines, the suite's ten fingerprints and 337
measurement lines, and three CLI payloads identical across presets and
identical to P17-REFMOD-001's run. The patch test and the axial bar to
1e-15 against gates of 1e-9; the mean axial stress under pressure and
self-weight exactly; every solved model in force and moment equilibrium
under the production default, RM-FEA-03's `eta_M` 5.98e-13 the largest.
The cantilever converges as a trend (3.8%, 4.7%, 4.8% of Timoshenko,
monotone, error falling) and no more is claimed. **No tolerance was added
or loosened**: the three new gates assert byte identity, set equality and
bit-exact fingerprints.
[REFERENCE_MODELS.md](REFERENCE_MODELS.md), [EQUILIBRIUM.md](EQUILIBRIUM.md),
[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Three-preset qualification

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0          (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext              0       0   29m27s       1 edge      31m13s   4009/4009
release-ext            0       0   38m28s       1 edge      29m16s   4009/4009
debug-shared-ext       0       0   30m26s       1 edge      31m37s   4009/4009

REPEAT (5x each of 1138 selected tests, back to back)
release-ext            0                            1138/1138   1101.68 s
debug-ext              0                            1138/1138   1232.44 s

warnings, all three clean builds   0   (-Werror, 658 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
```

17 stages, 3h49m57s, one uninterrupted detached run: 19:16:23 to 23:06:20.
Before it, the restored tree passed the whole suite in debug-ext
(4009/4009, 1880.51 s). [FINAL_REGRESSION.md](FINAL_REGRESSION.md).

**Cross-preset equivalence**: measured on this candidate's binaries, bit for
bit, and recorded as a measurement rather than promoted to a guarantee.
[CROSS_PRESET.md](CROSS_PRESET.md). **Determinism**: 1138 x 5 in two
presets, fresh-document fingerprints, and no source of non-determinism in
the module. [DETERMINISM.md](DETERMINISM.md).

## Final adversarial review

```text
QUESTIONS                 44  (30 from the brief, 14 further)
FINDINGS                  13
DOCUMENT DRIFT            12  eleven ADR sentences (amended) and one stale
                              README status block (addendum); none a gate
                              failure, because the property each document
                              was gating on is true
TEST DEFECT                1  in this milestone's own new file, found by its
                              first run and fixed before the freeze (T1)
PRODUCTION DEFECTS         0
GATE-BLOCKING              0
OWNER DECISIONS CARRIED    3  F1, F2, F3 -- unchanged
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

Genuine remaining limitations, none of which violates a P17 gate.

```text
Linear Tet4 locks in bending (F2), because OCCT never subdivides a
  developable wall along its ruling and every wall tetrahedron is a needle
  the full length of the body (F3). Bending on a prismatic body is a trend
  BetterCAD measures, not a number it certifies, until a quadratic element
  or a surface mesher that subdivides along a ruling exists.

The 1e-12 moment-equilibrium threshold is crossed by a correct bending
  solve of ~2500 needle elements (F1). The committed cantilever passes it;
  revising a qualified, measured threshold is the owner's ADR-level decision.

The residual gate is pinned by one synthetic test, because no physics case
  can reach it on a backward-stable factorisation. Recorded in MUTATION.md.

Linear static only: no nonlinearity, contact, buckling, dynamics, thermal
  stress, shells, beams, Tet10 or multi-solid assemblies -- refused rather
  than approximated (ADR-034).

The Qt inspector surface is not wired: apps/bettercad draws nothing
  structural. The headless display layer is qualified; a pressure, traction
  or gravity glyph is data and not drawn; there is no logarithmic scale.

A NodalForceLoad is mesh-local and refuses the save (ADR-048), by decision.

A drilled hole's cylindrical wall carries no FaceName, so it cannot be
  loaded or restrained; the refusal has no geometric fallback. P16's carried
  naming limit; permanent semantic topology is P21's.

Configuration-driven analysis is BLOCKED BY DESIGN, as meshing is, until
  configuration regeneration rebuilds bodies (carried defect 2).

Eigen is MPL-2.0 and the project's own licence remains the owner's to
  choose (carried constraint 8).

Five stale comments and one stale test name are recorded in ADR_AUDIT.md
  rather than edited, because this qualification changed no production file.

Sanitizer coverage is unavailable on this MinGW toolchain; the dependency
  DOWNLOAD path is unverifiable here (carried constraints 6 and 7).
```

## Result

```text
RESULT:   PASS
P17:      QUALIFIED
EVIDENCE: this directory; qualification/qualification-times.txt for the run
TREE:     the eight-path fingerprint in FREEZE.md, identical before and after
          the run and read back from the closeout commit
COMMIT:   BetterCAD: qualify P17 structural FEA -- recorded in FREEZE.md's
          commit record and in the final report
```

## Revision

First issue, 2026-10-15.
