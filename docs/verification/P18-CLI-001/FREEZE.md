# P18-CLI-001 — freeze

The tree that was built and tested is the tree that was committed. Everything below is read back from files this directory keeps.

## The qualified tree

The eight paths `qualify.cmd` fingerprints (`apps include src tests examples cmake CMakeLists.txt CMakePresets.json`), taken from a **scratch git index** so the working tree and the real index are not touched:

```text
base commit      2ffba66f6676fe459de225ea0839b21b40fa8681   (HEAD == origin/main at the start; tree f82824e07a87b6fc15183ae015cfe8c3dba7b6e1)
scratch tree     c0f4abc50a8ea8598dd6bc34684e79ecf4231c78

apps              b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117
include           cb59f4ab38f38f65e9caa633b674ab16e0c36543
src               d90a0f33e379ef71bd4a34f1ca6c29e6f768a4ed
tests             d73ceca94659c58d38d2fab44c8801dd9db2afc3
examples          0934015777cbb002ac29ec8ef8709cb26fb93705
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

* **Before the run** — `qualification/freeze-cli-pre.txt` (taken 2026-10-25 19:23, four minutes before launch), and `qualify.cmd`'s own "before the first build" record in `qualification/qualification-times.txt`.
* **After the run** — `qualification/freeze-cli-post.txt` (taken 2026-10-26 02:12) and `qualify.cmd`'s "after the last test run" record. The two freeze files have **208 lines each and no difference** (timestamp line excluded); all eight subtree hashes at the start, at the end of the run and in the freeze are the same.
* **In the commit** — the commit message of the milestone commit and the PASS block in `TODO.md` give the commit hash; the eight subtree hashes read back from that commit are the ones above (checked at commit time with `git rev-parse <commit>:<path>`; see the report).

## The fingerprints

All four are SHA-1 blob hashes (`git hash-object`) of a text built from the sources, identical in `freeze-cli-pre.txt` and `freeze-cli-post.txt`.

| Fingerprint | Value | What it covers |
|---|---|---|
| `P18_CLI_CONTRACT_FINGERPRINT` | `079ef49cf05bb6cb2af7e7b43793860553d54184` | the 9 files that define the contract: the entry (`ThermalAnalysisRun.hpp/.cpp`), the three CLI files, and the four shared files that register the verbs (`Commands.hpp`, `Cli.cpp`, `Edits.hpp`, `EditRegistry.cpp`) |
| `P18_CLI_COMMAND_SCHEMA_FINGERPRINT` | `10f7b13e709646bbb8030914f62d53eb809788f2` | the 24 lines of the command schema: every usage string, edit verb, command-table entry and option set |
| `P18_CLI_STRUCTURED_OUTPUT_FINGERPRINT` | `d878b4a2b584bf0e7177f4e758022455768fbdfc` | the 204 payload keys in the order the reports set them, plus the whole `ThermalOutput.hpp` |
| `P18_CLI_EXIT_POLICY_FINGERPRINT` | `73ec8abbc205b81678a25398f8b195439ff7763d` | the three-code `ExitCode` enumeration, every diagnostic code the thermal files can emit, the exit statements and the edit-failure code map (40 lines) |

Supporting fingerprints in the same file: `THERMAL_CLI_OWNERSHIP_FINGERPRINT` `07b681bef0c599e9b754aa4e46b4c54780cd8dc7` (the gate and its 512 fixture files), `P18_CLI_WORKFLOW_FINGERPRINT` `c90c1b27261e81e173ccea739380e1a55176b956` (the workflow, the tests, the guard and the two example documents), `ADR_057_DEPENDENCY_FINGERPRINT` `724f991a7ea897c4410366043fa3a2c9a7fbe602` (the two CMake lists and the two layering checks).

## What changed in the existing tree

`freeze-cli-pre.txt` compares every `src`, `include` and `apps` file with `HEAD`: **520 files, 509 byte-identical, 6 modified, 5 new, 0 unexpected.**

| | Files |
|---|---|
| modified (the registration of the verbs, and the thermal source list) | `apps/bettercad_cli/Cli.cpp`, `Commands.hpp`, `Edits.hpp`, `EditRegistry.cpp`, `CMakeLists.txt`, `src/thermal/CMakeLists.txt` |
| new | `include/bettercad/thermal/ThermalAnalysisRun.hpp`, `src/thermal/ThermalAnalysisRun.cpp`, `apps/bettercad_cli/ThermalReports.cpp`, `ThermalEdits.cpp`, `ThermalOutput.hpp` |

**No existing thermal source or header changed.** The two qualified thermal test files `ThermalDataTests.cpp` and `ThermalCommandTests.cpp` are byte-identical to `HEAD`.

## Harness

`qualify.cmd` `d313a64070718c44fae290ac042fe259d1a03c8b` and `verify-harness.cmd` `11855b8ff891093b021d1e0351761283d0eedd24` are byte-identical to the ones every milestone since P16-SIZE-001 has used. `run-qualification.cmd` was checked before the launch: all 103 lines begin with `rem`, `set`, `call`, `echo`, `setlocal`, `endlocal` or `@echo`, and it was not touched while it ran.

## What is not frozen

`TODO.md`, `ROADMAP.md`, `docs/` and `.claude/` are outside the eight paths. They were edited after the run, as the harness expects.

## The commit

Qualification finished 2026-10-26 02:06:03 with 0 stages failed; the harness printed the eight subtree hashes after the last test run and they equal the ones above. The tree was committed as `ebca776` (tree `392363004d6a527ba8db488b7c65cf8ac9404e7b`), "BetterCAD: add headless thermal CLI", and the commit script read the eight subtree hashes back from the commit:

```text
apps                    == frozen   b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117
include                 == frozen   cb59f4ab38f38f65e9caa633b674ab16e0c36543
src                     == frozen   d90a0f33e379ef71bd4a34f1ca6c29e6f768a4ed
tests                   == frozen   d73ceca94659c58d38d2fab44c8801dd9db2afc3
examples                == frozen   0934015777cbb002ac29ec8ef8709cb26fb93705
cmake                   == frozen   5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt          == frozen   3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json       == frozen   3229f0f89b236744d7d5c082bed47c28b718288e
ALL EIGHT == FROZEN
```
