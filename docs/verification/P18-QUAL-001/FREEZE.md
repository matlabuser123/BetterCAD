# P18-QUAL-001 - the freeze (brief sections 91-97)

The qualified tree is the working tree, frozen before the run and read back after it. The eight-path fingerprint is exactly the one `qualify.cmd` computes (a scratch index: `read-tree HEAD`,
`add -A` of `apps include src tests examples cmake CMakeLists.txt CMakePresets.json`, `write-tree`, then one `write-tree --prefix` per directory), so the harness and the freeze agree by construction.

`freeze-qual.ps1` was run twice before the run (`qualification/freeze-qual-pre.txt`, `freeze-qual-pre2.txt`: identical apart from the timestamp) and once after (`freeze-qual-post.txt`: identical to the first,
**pre == post**). `qualification-times.txt` independently records the same eight hashes before the first build and after the last test.

| path | frozen hash | at the start of the run | after the last test |
|---|---|---|---|
| apps | `b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117` | equal | equal |
| include | `f40f760dfc384bb09f3e8364761db1564474d291` | equal | equal |
| src | `7fae8afb5bdaebccd2fe02a7deaaf2c4e891fd03` | equal | equal |
| tests | `ce05767c5315f9f8452d630786e948b924848693` | equal | equal |
| examples | `4da234ebf4779f7c16d5df0075f106f7e3d4c958` | equal | equal |
| cmake | `5a382115d7ed7775189d40b55e40f318eaae6cc7` | equal | equal |
| CMakeLists.txt | `3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd` | equal | equal |
| CMakePresets.json | `3229f0f89b236744d7d5c082bed47c28b718288e` | equal | equal |

Candidate: `ece7691063531cf033403829fa70ab40233c3673` (HEAD, tree `fcce738bc6d7c8c6eb3ebcd15d16f8a3680803d3`) plus the working tree; scratch tree `5386f4fb2cd54bc6a718501502efc293fa0b8ae3`.

## What differs from HEAD in the eight paths (all expected, all in this milestone's scope)

| file | change |
|---|---|
| `src/io/json/ThermalAnalysisJson.cpp` | D-A: the writer refuses a target the reader would refuse (55 added lines) |
| `src/thermal/ThermalValidation.cpp` | D-F: `classifyThermalMesh` refuses a metric no element was judged for |
| `include/bettercad/renderer/ThermalInspection.hpp` | D-B: `resultState` has no default; the records default to `NoAnalysis` |
| `tests/qualification/P18ThermalQualificationTests.cpp` | new: the seven final-gate cases |
| `tests/io/ThermalAnalysisFileTests.cpp`, `tests/thermal/ThermalValidation{Tests,TestSupport,LawTests}`, `ThermalSolveTests.cpp`, `tests/cli/ThermalCliTests.cpp` | the tests that pin the fixes and close audit gaps |
| `tests/renderer/ThermalViz*Tests.cpp` (5), `tests/compile_fail/ThermalVizMisuse.cpp`, `tests/compile_fail/CMakeLists.txt` | the 31 call sites that now state the result state; the compile-fail case |
| `tests/architecture/fixtures/valid/src/structural/Analysis.cpp` | the structural -> thermal direction in the valid tree |
| `tests/CMakeLists.txt`, `tests/cli/ZeroMatchGuard.cmake` | registration and the zero-match groups |

Nothing under `examples/`, `apps/` or `cmake/` changed. **Three production files changed**, and each is the fix of a defect found, reproduced, and pinned by a test that fails against the old file (`FINDINGS.md`).

## The eighteen contract fingerprints (hash of the sorted "path git-hash" list; the file lists are in `freeze-qual-pre.txt`)

| # | contract | fingerprint |
|---|---|---|
| 1 | architecture (ADR-049..057, layering and target checkers, the module CMake files) | `9e52682f697706b3417c4f583697243205a9aba2` |
| 2 | authority (data, analysis object, material, target, the validation-ownership gate) | `b9fafa8a56e5e09ff48f9d87d0ebea19dbca671b` |
| 3 | currentness (every source record, the display state, the final-gate test) | `3386b23c42080bd96ce85fd7e30fb2d170b0756f` |
| 4 | DOF | `ff424646cf2b67e5e2a2f88bfe8db6ee6e6f1588` |
| 5 | element (conduction, capacity, convection kernels) | `513f59e0eaeb848c533325ffb224a616f7ca3f29` |
| 6 | loads and boundary conditions | `22180f64bc02a85bd8f9a35c62b6936cf9a35fdb` |
| 7 | assembly | `38850a806353913001692d2708e367841edef97d` |
| 8 | solver (steady solve and the `linalg` kernel) | `fe37b737e5f82a4be24026afc947fb73c0e4152f` |
| 9 | transient | `f244f54d3e309e11e2c1c7a8e81b14483eb061a9` |
| 10 | POST | `d14c0868a27f0e90a9e7a7743c9242da5f190785` |
| 11 | BALANCE | `e1a374a4f22a16c52584bef753db3f304ef5ee63` |
| 12 | VALIDATION (the validator and the single run entry) | `61d497e2dde9f35584b85a003721c0059a33e68e` |
| 13 | VIZ (headless view, the OCCT adapter, the viewer header) | `ddeafa58aa47ee9e31b867696f5167f834674c24` |
| 14 | commands | `12c7ef7d80c367c6d2dd332eba45e77537e4f0ef` |
| 15 | persistence | `3b564e249389fe1834025f0774fba7872ee5e74e` |
| 16 | CLI | `f6f49c88683c210baf4d5e2553b620581fa03f15` |
| 17 | reference suite (documents, tests, oracle, workflows, zero-match guard) | `ab64f4f0ba9bfdcef119d3b308e10ab8380327a6` |
| 18 | tolerance (24 named constants and settings defaults, extracted by pattern) | `d7287bdfd61db49a8b774c1a767541aa8e1c5e01` |

Test inventory at the freeze (`TEST_CASE(` occurrences): thermal 879, reference 348, cli 300, renderer 254, io 374, qualification 25, linalg 10.

## Harness

`qualify.cmd` (`d313a64070718c44fae290ac042fe259d1a03c8b`) and `verify-harness.cmd` (`11855b8ff891093b021d1e0351761283d0eedd24`) are byte-identical to the ones P16-SIZE-001 settled on: **fifty in a row**. `verify-harness.cmd` was
run before the launch: a failed stage gave `qualify.cmd` exit 2, and a zero-match repeat filter was counted as a failed stage. Every line of `run-qualification.cmd` begins with `rem`, `set`, `call`, `echo`, `setlocal`, `endlocal` or `@echo`
(checked before the run: 0 non-conforming lines).

## Qualified == committed

Committed as `b5dce0773b620ef4c628ae64c534e58a9f66b4ce` (`BetterCAD: qualify P18 thermal analysis`). The commit script recomputed the eight hashes from a scratch index immediately before committing and required each to equal the frozen one, then read the eight subtree hashes of the commit back: **apps, include, src, tests, examples, cmake, CMakeLists.txt and CMakePresets.json of the commit are all equal to the frozen values above. Qualified tree == committed tree.**
