# P18-REFMOD-001 — Freeze

The qualified tree is the eight-path tree `apps include src tests examples cmake CMakeLists.txt CMakePresets.json`, fingerprinted from a **scratch git index** exactly as `qualify.cmd` computes it (the working tree is never trusted by name). Taken before the first build (`qualification/freeze-ref-pre.txt`, twice, identical), by `qualify.cmd` after the last test run (`qualification/run-qualification.out`) and again after the qualification (`qualification/freeze-ref-post.txt`): **all three are equal.**

| | |
|---|---|
| HEAD at the freeze | `b274df23a0f3e4c1f62731b8fb0e94b487836d01` (P18-CLI-001, pushed and verified) — HEAD tree `ff298b1ef6576d44f6c2892c589ff6a36727e08d` |
| Scratch tree (qualified) | `ba86e2f44dd023529775d4955108f90726f16b9c` |
| `apps` | `b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117` (== HEAD) |
| `include` | `cb59f4ab38f38f65e9caa633b674ab16e0c36543` (== HEAD) |
| `src` | `d90a0f33e379ef71bd4a34f1ca6c29e6f768a4ed` (== HEAD) |
| `tests` | `897e247d07f15093f326e4e8707e726a5d141b04` |
| `examples` | `4da234ebf4779f7c16d5df0075f106f7e3d4c958` |
| `cmake` | `5a382115d7ed7775189d40b55e40f318eaae6cc7` (== HEAD) |
| `CMakeLists.txt` | `3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd` (== HEAD) |
| `CMakePresets.json` | `3229f0f89b236744d7d5c082bed47c28b718288e` (== HEAD) |

**NOTHING QUALIFIED CHANGED.** `src`, `include` and `apps` — **520 files — are byte-identical to HEAD** (`equal 520, different 0, new 0`); so are `cmake`, the root `CMakeLists.txt` and `CMakePresets.json`. Only `tests` and `examples` differ. No production file was edited at any point of the milestone, including by the mutation campaign (every mutated file was restored byte for byte; `git status` on `src include apps` was empty afterwards).

## Milestone fingerprints (git blob hashes of canonical text lists)

| Fingerprint | Value | Covers |
|---|---|---|
| `P18_REFMOD_CONTRACT_FINGERPRINT` | `f682228cf9b89d5d8a55f7c121304fefa61b3c64` | the reference-document builders, catalog, runner loop and its build list (`examples/reference_models/Thermal*`, `main.cpp`, `CMakeLists.txt`) |
| `P18_REFERENCE_SUITE_FINGERPRINT` | `40b8a0aac2e5093e7ce9a29a0111ae11ec0a88a7` | the eight reference test sources and two fixture headers, the workflow script, the zero-match guard and `tests/CMakeLists.txt` |
| `P18_ANALYTICAL_ORACLE_FINGERPRINT` | `24abd7060b1138e23029883bf15d0a77583af65e` | `tests/reference/ThermalAnalytic.hpp` and the independence gate |
| `P18_REFMOD_TOLERANCE_FINGERPRINT` | `856d9044504c52287c081b4ae09e9baed5a0699c` | every `Tolerance{…}` literal, `kClassA/B` and numeric `CHECK` bound in the reference sources (121 lines) |

All four are equal in `freeze-ref-pre.txt` and `freeze-ref-post.txt`. They exist so that a later milestone that changes a tolerance, the oracle or a reference document changes a named hash rather than a diff nobody reads.

## Harness

`qualify.cmd` `d313a64070718c44fae290ac042fe259d1a03c8b` and `verify-harness.cmd` `11855b8ff891093b021d1e0351761283d0eedd24` are byte-identical to the ones every milestone since P16-SIZE-001 has used (forty-nine in a row with this one); `verify-harness.cmd` was run before the qualification and passed. `run-qualification.cmd` satisfies the line-prefix rule (checked before the run, 0 violations) and was not edited while running.

## Qualified == committed

Qualification finished 2026-10-26 12:30:01 with 0 stages failed (6 h 15 min 48 s). The feature commit is **`04751f65126eac0e60ca17881f01f68cdc910d00`** (`BetterCAD: add thermal reference model suite`), its tree `5c9d33d750d577782070881740fec11e529bdaca`. The eight subtree hashes read back **from the commit** equal the frozen ones:

| Path | Subtree hash in the commit (== frozen) |
|---|---|
| `apps` | `b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117` |
| `include` | `cb59f4ab38f38f65e9caa633b674ab16e0c36543` |
| `src` | `d90a0f33e379ef71bd4a34f1ca6c29e6f768a4ed` |
| `tests` | `897e247d07f15093f326e4e8707e726a5d141b04` |
| `examples` | `4da234ebf4779f7c16d5df0075f106f7e3d4c958` |
| `cmake` | `5a382115d7ed7775189d40b55e40f318eaae6cc7` |
| `CMakeLists.txt` | `3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd` |
| `CMakePresets.json` | `3229f0f89b236744d7d5c082bed47c28b718288e` |

A second commit, `BetterCAD: record the P18-REFMOD-001 commit`, adds this section and the commit hash to the TODO.md PASS block; it changes only those two files.
