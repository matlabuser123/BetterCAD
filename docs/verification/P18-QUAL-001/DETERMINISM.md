# P18-QUAL-001 - repeated determinism (brief sections 114-117)

**Verdict: the same inputs give the same bits, in one process, across processes, and across five repetitions of 2561 tests in Release and in Debug.**

## The repeat

`qualify.cmd` selects, by `QUALIFY_REPEAT` (recorded in `run-qualification.cmd`), the groups the thermal phase could have disturbed - `architecture`, `unit.Linalg`, `unit.Structural`, `unit.Thermal`, `unit.P18Qualification`, `unit.P17Qualification`,
the mesh-side groups, `refmod.`, `gui.`, `cli.thermal.`, `cli.structural.`, `cli.mesh.` - counts them before running (**2561 tests, on each of the two repeat presets**, recorded in `qualification-times.txt`; a filter matching none
is a failed stage), and runs them with `ctest --repeat until-fail:5`.

| preset | tests selected | result | time |
|---|---:|---|---:|
| release-ext | 2561 | 100% passed, every repetition | 1665 s |
| debug-ext | 2561 | 100% passed, every repetition | 1937 s |

(`ctest-repeat-<preset>.log`.) The two executables are the ones built by this run's clean.

## What "the same" means, and where it is asserted

| level | assertion |
|---|---|
| bitwise within a process | `ThermalAssembly_BlockBitFingerprintForCrossPresetComparison`, `ThermalBalance_RepeatedAggregationDeterministic`, `ThermalLoad_RepeatedPreparation...`, `ThermalBC_RepeatedPreparation...`, `ThermalReference_Determinism_*` (two), `ThermalRun_TwoRunsOfOneStateAreIdentical` - a fingerprint over the raw bits of every array |
| a fresh run reproduces the bits of an earlier one | `P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches` (the remesh gives the original fingerprint; the undo gives r3's; a tolerance change gives r5's) and `_AFailedRegenerationNeverYieldsACurrentResult` (the recovered run equals the original to the last bit) |
| across processes | `cli.thermal.workflow` (74 processes) and `refmod.thermal.workflow` (58): `solve` and `solve-again` in two separate processes print byte-identical payloads; the whole-file hash of every document is identical before and after |
| across presets | `CROSS_PRESET.md`: the 20 documents, 59 payloads and 72 evidence lines identical to the bit |
| the file | `ThermalPersistence_SaveLoadSaveIsByteIdenticalAndAFixedPoint`, `ThermalPersistence_TheGoldenSectionsHaveFixedBytes` |
| no hidden state | `ThermalReference_Determinism_*` runs the chain twice in one process with a different history in between; the mesher's process-unique mesh ids differ and no result depends on them |

## What is deliberately not deterministic

The `MeshId` is a process-local counter, so two runs have different mesh stamps; that is exactly why an old result is stale after a remesh even when every number is the same. No printed number depends on it.
