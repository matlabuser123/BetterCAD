# P18-QUAL-001 - TODO audit (brief sections 6-8)

Method: `milestone-audit.ps1` reads `TODO.md`, splits it into the eighteen P18 blocks (`# P18-xxx-001` up to the next `# P18-` or `# Future Phases`), and counts for each the ticked boxes (`* [x]`),
the unticked boxes (`* [ ]`), the dated `PASS 20..` block, and the feature and record commit hashes the block names. Its output is `MILESTONE_AUDIT.md`.

| check | result |
|---|---|
| P18 blocks found in `TODO.md` | 18 of 18 (`ARCH DATA MAT DOF ELEM LOAD BC ASSEMBLY SOLVE TRANSIENT POST BALANCE VALID VIZ CMD PERSIST CLI REFMOD`) |
| unticked boxes in any predecessor block | **0** (ticked: 19, 19, 19, 12, 21, 18, 17, 20, 15, 21, 16, 14, 18, 21, 22, 22, 27, 24 = 345) |
| every predecessor block has a dated PASS block | yes, 18 of 18 |
| every predecessor block's `qualification-times.txt` ends `0 stage(s) failed` | yes, 18 of 18 |
| every predecessor's feature commit and record commit exist and are ancestors of `HEAD` | yes |
| the P18-QUAL-001 block at the start of this milestone | 43 boxes, all unticked, the `P18 Final Gate` text, 33 adversarial questions, the carried constraints |
| `# Future Phases` is the last section of `TODO.md` | yes (line 1883 of 1900+ at the start; the closeout leaves it last) |
| the next phase in `TODO.md` | `P19 - CFD Integration`, **not authorized** |

## The 43 boxes of this block, and where each is evidenced

| box | evidence |
|---|---|
| Freeze final P18 tree | `FREEZE.md`: the eight-path scratch-index fingerprint, 18 contract fingerprints, frozen twice and read back after the run |
| Audit every P18 milestone / Verify all TODO items complete | this page, `MILESTONE_AUDIT.md` |
| Verify all P18 ADRs | `ADR_AUDIT.md` |
| Audit authority | `AUTHORITY_MODEL.md` |
| Audit currentness | `CURRENTNESS.md` |
| Audit DOF model | `DOF_MODEL.md` |
| Audit Tet4 formulation | `ELEMENT_FORMULATION.md` |
| Audit loads, BCs | `LOADS_BCS.md` |
| Audit global assembly | `ASSEMBLY.md` |
| Audit steady solver, transient solver | `STEADY_SOLVER.md`, `TRANSIENT_SOLVER.md` |
| Audit temperature, gradient, heat-flux recovery | `POST.md` |
| Audit heat balance | `BALANCE.md` |
| Audit validation policy, stale-input protection | `VALIDATION.md`, `CURRENTNESS.md` |
| Audit visualisation, commands, persistence, CLI | `VIZ.md`, `COMMANDS.md`, `PERSISTENCE.md`, `CLI.md` |
| Re-run all thermal reference models | `REFERENCE_MODELS.md` (RM01-RM10 on three presets) |
| Independent analytical validation | `ANALYTICAL_VALIDATION.md` |
| Steady residual, heat balance, patch test, slab, convection, transient benchmark, singular model, stale mesh PASS | `REFERENCE_MODELS.md`, `FINAL_GATE.md` (the table maps each to its cases) |
| Clean Debug, Release, Debug-shared qualification | `qualification/` logs, `README.md` |
| Repeated determinism | `DETERMINISM.md` |
| Cross-preset equivalence | `CROSS_PRESET.md` |
| Final adversarial review | `ADVERSARIAL_REVIEW.md` |
| Zero-match protection | `qualification/postrun-summary.txt` (the zero-match table), `README.md` |
| 0 unexpected warnings | the build logs: `warning:` count 0 on each preset |
| Qualified tree == committed tree | `FREEZE.md`, the read-back after the commit |
| Evidence recorded / Mark P18 qualified | this directory; the TODO closeout |

## Two TODO texts to read carefully

* "**Can stale binaries produce qualification evidence?**" - not unless a binary is not rebuilt; every stage cleans the preset, rebuilds, rebuilds again (nothing to do), and
  `postrun-qual.ps1` proves each binary is newer than that preset's clean and than the newest source file (`FRESH_BINARY.md`).
* "**For every credible defect: reproduce, regression test, root cause, general fix, requalify the affected milestone, rerun P18-QUAL.**" - done for D-A, D-B and D-F in `FINDINGS.md`. The
  requalification of the affected milestones (PERSIST, VIZ, VALID) is the three-preset run of this milestone, which executes the **whole** suite of every one of them, not a subset.
