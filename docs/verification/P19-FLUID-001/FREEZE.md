# P19-FLUID-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: c8b53f6a76e3088831679084c85e34e66c311e73  (P19-DATA-001's record) plus the
           working tree: P15's viscosity and flow consumer (core, features, io,
           CLI), cfd's fluid resolution, the tests, ADR-062 and this directory --
           git status recorded in qualification/freeze-fluid-pre.txt
RAN:       Tue 27/10/2026 22:35:47 -> Wed 28/10/2026 05:23:21  =  6h47m34s, first attempt
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a640, verify-harness.cmd 11855b8f, launch.cmd f11039e6,
           all byte-identical to P19-DATA-001's; verify-harness.out: PASS;
           run-qualification.cmd checked line by line by whole word before launch
MACHINE:   the hasib machine; OneDrive restarted before the launch (18.1 GB ->
           0.08 GB; 60.6 GB commit free)
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **5919 / 5919** | **5919 / 5919** | **5919 / 5919** |
| `warning:` / `error:` | 0 / 0 | 0 / 0 | 0 / 0 |
| objects compiled | 753 (778 build steps) | 753 (778) | 753 (778) |
| configure | 6 s, exit 0 | 11 s, exit 0 | 6 s, exit 0 |
| clean | exit 0, 777 files | exit 0, 774 files | exit 0, 787 files |
| build | 30m52s | 36m39s | 30m51s |
| no-op rebuild | exit 0, 1 edge of 8 | exit 0, 1 edge of 8 | exit 0, 1 edge of 20 |
| ctest | 5001.59 s | 4764.49 s | 4962.22 s |

```text
repeat release-ext   2783 tests x 5   1669.34 s   exit 0, 100% passed
repeat debug-ext     2783 tests x 5   2070.39 s   exit 0, 100% passed
```

Every count was **measured**, and each reconciles against P19-DATA-001:

```text
suite    5893 -> 5919   +26:  11 unit.FluidProperty_*, 13 unit.CfdFluid_*, 1
                              unit.MaterialCli_Viscosity_*, 1 process test
                              (cli.material.completeness.incompressible-flow);
                              CfdInput_AcceptsADensityWithNoViscosity... replaced
                              one-for-one by CfdInput_RefusesADensityWithNoViscosity;
                              compile_fail.prov.no-cfd-consumer renamed, not added
objects   750 ->  753    +3:  CfdFluid.cpp, FluidPropertyTests.cpp, CfdFluidTests.cpp
                              (5 cfd source objects per preset)
steps     775 ->  778    +3:  the three objects
repeat   2622 -> 2783  +161:  the filter ADDED unit\.Fluid, unit\.Provenance,
                              unit\.CustomMaterial, unit\.MaterialFile and
                              unit\.MaterialCli (the P15 suites this milestone
                              edited); unit\.Cfd now also selects the 13 CfdFluid cases
```

Because core's material headers changed, every module recompiled from clean in every
preset. The no-op rebuild re-ran one edge, "Checking git revision". **0 unexpected
warnings.** The harness's stderr is 270 lines, all git line-ending notices.

## The tree that was qualified is the tree that will be committed

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   MOVED (the CLI vocabulary)
include            833e8b294f7b83ac2f926106975a8720afe0532a   MOVED
src                27c6be7371eef86e11786172fa4cb0c6bf910a64   MOVED
tests              2ce7a53c5b3a9a13d879ead9c7ef7c45595e19bc   MOVED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   == HEAD (c8b53f6)
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == HEAD
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == HEAD
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == HEAD
scratch tree       18936e0089916c5139ae24e458ef45aa490489d2
```

**Before == after.** The harness took the trees at 22:35:47 and again after the last
test run. `freeze-fluid.ps1` ran twice before the launch (identical) and once after,
and all 90 lines equal `freeze-fluid-pre.txt`, including the milestone's own
fingerprints:

```text
P19_FLUID_P15_FLUID_PROPERTY   536d7746...   ThermalProperties, Completeness, Materials,
                                              MaterialJson, the CLI vocabulary
P19_FLUID_CFD_FLUID            72ada103...   CfdFluid, CfdAnalysis, CfdResult, CfdData,
                                              CfdAnalysisObject, the cfd CMake
P19_FLUID_FLUID_TESTS          a4053f97...   every test file this milestone added or
                                              edited, the registrations, the guard
P19_FLUID_ADRS                 8c20657b...   ADR-062
```

## The fresh-binary proof

Each binary is newer than its preset's clean and newer than the newest file of the
frozen tree (`src/features/material/Materials.cpp`, 22:11:29):

```text
preset            binary                    bytes          built      sha256
debug-ext         cleaned 22:36:05.80
                  bettercad-cli.exe           423 095 764  23:04:56   e5578285d08b1c19...
                  bettercad_tests.exe       1 476 628 029  23:06:50   6428404798cc66b7...
release-ext       cleaned 00:30:33.52
                  bettercad-cli.exe            11 602 958  01:06:16   da68e400bdb61ca2...
                  bettercad_tests.exe          62 482 869  01:06:44   6fea567abfed916d...
debug-shared-ext  cleaned 02:26:45.56
                  bettercad-cli.exe            49 033 068  02:55:44   ddeb2c3dc48d0f78...
                  bettercad_tests.exe       1 081 099 934  02:57:31   c43ee1ab2db5281d...
                  libbettercad_cfd.dll          9 744 566  02:38:28   5025b60175fcea1c...
```

Full hashes are in `qualification/postrun-summary.txt`. The CLI binaries grew, as
they should: the CLI learned a property code and a consumer.

## Zero-match protection

There are 246 groups in the guard. In every preset, every group's discovered count
is at least its minimum and its executed count equals its discovered count. **Groups
failing either: 0.** An impossible filter discovers 0. The new groups: `unit\.FluidProperty_`
11, `unit\.CfdFluid_` 13, `CfdInput_RefusesADensityWithNoViscosity` 1,
`unit\.MaterialCli_Viscosity_` 1, `cli\.material\.completeness\.incompressible-flow` 1,
`compile_fail\.prov\.no-turbulent-flow-consumer` 1. Each was discovered and executed
in every preset.

## Cross-preset equivalence

Each preset ran the fluid, CFD and P15 material cases with its own test binary after
the harness, and ran the two compile-fail groups through its own CTest:

```text
debug-ext          All tests passed (2769 assertions in 77 test cases)   compile-fail 43/43
release-ext        All tests passed (2769 assertions in 77 test cases)   compile-fail 43/43
debug-shared-ext   All tests passed (2769 assertions in 77 test cases)   compile-fail 43/43
golden fingerprint cfd-definition-v1:20707079b32e4964 in all three (unchanged from P19-DATA-001)
checkers           562 files, 0 layering violations; cfd links neither thermal nor structural
```

## The commit record

```text
commit             4e57b0231f9058c693e809f1d71b9624110e51d2
                   "BetterCAD: resolve CFD fluid properties through P15", parent c8b53f6
scratch tree       18936e0089916c5139ae24e458ef45aa490489d2   == frozen
apps               fc398a3d2e524178cf38e8b667236379d8381086   == frozen
include            833e8b294f7b83ac2f926106975a8720afe0532a   == frozen
src                27c6be7371eef86e11786172fa4cb0c6bf910a64   == frozen
tests              2ce7a53c5b3a9a13d879ead9c7ef7c45595e19bc   == frozen
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

All eight are equal: **qualified tree == committed tree**.

**How it got there, recorded rather than smoothed over.** `commit-fluid.ps1` was
copied from P19-DATA-001's script, whose staging list was hand-written as
`docs include src tests`. Every earlier milestone left `apps/` untouched, so the list
never mattered. This milestone changed `apps/bettercad_cli/MaterialVocabulary.cpp`.
The first commit (`04ec009`) therefore left that file out, and the script's own
post-commit check printed `apps ... != frozen` and `MISMATCH AFTER COMMIT`. That
commit was never pushed. It was amended at once to add `apps/`, giving `4e57b02`,
and all eight subtrees were read back equal to the freeze, as above. The script now
stages the eight qualified paths from its own `$paths` list instead of a
hand-written one. This record could be written only once the commit existed, so it
lands in the documentation commit that follows.
