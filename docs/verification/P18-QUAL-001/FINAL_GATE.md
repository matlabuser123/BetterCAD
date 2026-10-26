# P18-QUAL-001 - the P18 Final Gate, line by line

Each line of the gate in `TODO.md`, the evidence that decides it, and the verdict. "3 presets" means the clean Debug, Release and Debug-shared qualification of this milestone
(`README.md`, `qualification/`); the postrun tables are `qualification/postrun-summary.txt`.

| gate line | evidence | verdict |
|---|---|---|
| all P18 milestones PASS | `MILESTONE_AUDIT.md`: 18 of 18 blocks, 0 open boxes, 0 failed stages | PASS |
| CAD geometry remains authority | `AUTHORITY_MODEL.md`; search S8; `P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical` | PASS |
| P15 material remains canonical | searches S3-S6; `ThermalPersistence_TheMaterialStaysP15sAndIsNotCopiedIntoTheAnalysis` | PASS |
| P16 mesh remains derived | search S1; compile-fail `thermalids.*`; `ThermalPersistence_TheFileDoesNotFollowTheMeshOrTheResultSize` | PASS |
| P18 results remain derived | `ThermalPersistence_NoDerivedStateReachesTheFile`; the 20-document byte identity; mutation QP21 | PASS |
| stale geometry / stale mesh rejection | `CURRENTNESS.md`; `ThermalReference_RM10_*`; `P18Qualification_AFailedRegenerationNeverYieldsACurrentResult` | PASS |
| thermal DOF model | `DOF_MODEL.md` | PASS |
| Tet4 conduction and capacity formulation | `ELEMENT_FORMULATION.md`; mutations QP01-QP03 | PASS |
| conductivity matrix symmetry, thermal null mode | `ThermalElement_ConductionSymmetric` (exactly 0.0), `ThermalAssembly_PureConductionPreservesConstantNullMode`, `ThermalReference_RM09_*` | PASS |
| thermal loads, BCs, GeometryReference mapping | `LOADS_BCS.md` | PASS |
| global thermal assembly | `ASSEMBLY.md`; mutations QP04-QP06 | PASS |
| steady solver, residual validation | `STEADY_SOLVER.md`; mutations QP09, QP10, QP26 | PASS |
| transient solver, time-step validation, energy storage | `TRANSIENT_SOLVER.md`; mutation QP17 | PASS, with the F1 limitation recorded (`ADVERSARIAL_REVIEW.md`) |
| temperature, gradient, heat flux | `POST.md`; mutation QP08 | PASS |
| steady heat balance, transient energy balance | `BALANCE.md`; mutations QP11, QP28 | PASS, with F1 |
| thermal mesh-quality acceptance policy explicit | `VALIDATION.md`, `TOLERANCES.md`; D-F fixed | PASS |
| visual inspection | `VIZ.md`; D-B fixed | PASS (headless library; no GUI application exists) |
| undo/redo; generated arrays excluded from history | `COMMANDS.md`; mutation QP20 | PASS |
| persistence; generated arrays excluded as authority | `PERSISTENCE.md`; D-A fixed | PASS |
| CLI; CLI/core equivalence | `CLI.md`; mutation QP22 | PASS |
| reference models | `REFERENCE_MODELS.md` (RM01-RM10 on three presets) | PASS |
| independent analytical validation | `ANALYTICAL_VALIDATION.md`; `verify-independent.ps1` on three presets | PASS |
| thermal patch test | `ThermalReference_RM01_*` | PASS |
| Debug, Release, Debug-shared | `qualification/ctest-<preset>.log` | PASS |
| determinism | `DETERMINISM.md` | PASS |
| cross-preset equivalence | `CROSS_PRESET.md` | PASS |
| adversarial review | `ADVERSARIAL_REVIEW.md`: all 33 questions answered; three major findings, none a wrong number; F1 is open | PASS, with F1/F2/F3 open |
| zero-match protection | the zero-match table of `postrun-summary.txt` | PASS |
| 0 unexpected warnings | the build logs | PASS |
| qualified tree == committed tree | `FREEZE.md` | see FREEZE.md |

The phrase "PASS, with F1" is deliberate: the gate asks whether the phase behaves as specified, and it does - no accepted result is wrong. It does not ask whether the validation gate is
tight enough to accept every correct run, and one finding (F1) says it is not for small transient steps. That is reported, not hidden.
