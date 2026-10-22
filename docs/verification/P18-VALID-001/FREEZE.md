# P18-VALID-001 — the freeze

```text
CANDIDATE:   the working tree on top of HEAD bc62b0ce3e3ec12cdd884a617649f9e4dab617fe (P18-BALANCE-001 record, origin == HEAD)
HEAD TREE:   777657673b87fe35c55c69630cad33920a2d1b16
FROZEN:      2026-10-22 23:21, after the last change to a source, a test, a CMake file or a build rule
RULE:        any change to a source, a test, the policy, a tolerance, a severity, the currentness logic or a build rule after this point invalidates the evidence and
             requires a new freeze and a new run
```

## The eight-path fingerprint

Computed from a scratch index (`read-tree HEAD`, `add -A` of the eight paths, `write-tree`), exactly as `qualify.cmd` computes it before the first build and after the last test:

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175
include            5eeb3868b97419f70f6a7db132e9ba2d39d5cdee
src                8a8bfb7a15c77cbebc4eb8ba8c9a2f3f6dc76503
tests              b1ae3d16c902dadcb3a595d04ae471c77b44c6c1
examples           a955cf6511ce715fcb95bc06ef82b877535869e6
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
scratch tree  b15417a6f8035afa5be440cc0e252fecb4588963
```

## The fingerprints this milestone adds

```text
P18_VALIDATION_CONTRACT_FINGERPRINT 4a4f01fe3d6762b4ebf7023e04d3672f3695e060
P18_VALIDATION_POLICY_FINGERPRINT e47ba8e0625ad79464ca6ebc367757efdac9dff4
P18_THERMAL_MESH_ACCEPTANCE_FINGERPRINT ec012b397d494b92fb2bc75cbd2af091e68ad2eb
VALIDATION_OWNERSHIP_FINGERPRINT d10a0abce911007891db9b869cc7a7c421833044
ADR_050_DEPENDENCY_FINGERPRINT 6f286b23d7bf7ad425db52c5de96bd34cb39fc65
```

* **Contract**: `ThermalValidation.hpp` with the two headers whose types it returns or reads (`ThermalResult.hpp`, `ThermalAnalysisObject.hpp`).
* **Policy**: the rule order, `severityOf`, `ruleOf`, `thermalQualityThresholds` and the three constants. The runtime form is `p18-valid-v1:f97b835a659f144e`, which every report carries.
* **Mesh acceptance**: the constants, the thresholds and the two measurement tests they are derived from.
* **Ownership**: the checker and its seven fixtures. **Dependency**: the thermal, linalg and structural CMake lists and the two layering checkers (ADR-050).

## What was NOT changed

```text
untouched files: 63 equal 63 different 0
```

Every thermal source and header except the new pair and the CMake list, every file of `src/meshing` and `include/bettercad/meshing` (P16 is not changed), and the linalg and
structural solve files, compared by blob against `HEAD`. The list is in `qualification/freeze-valid.txt`.

## What changed

```text
blob src/thermal/ThermalValidation.cpp                          634d6420940fcec9c7ed16951823dda3c28f70ba
blob tests/thermal/ThermalValidationTests.cpp                   1b76da232301eac2fb5230aa4b14a688da2d6110
blob tests/thermal/ThermalValidationMappedTests.cpp             6ab3b74c016e8a132ed35ad1d5ece25bb8eebe61
blob tests/thermal/ThermalValidationLawTests.cpp                63bba7abff802641b3c79d284c3f285334fccd4b
blob tests/thermal/ThermalValidationTestSupport.hpp             1287439b6b5fbcd6969cb2fcae188c3928be4466
blob tests/reference/ThermalValidationSweepTests.cpp            8db6d9713febe877394e8c8ae0947640edd2b1c5
blob tests/compile_fail/ThermalValidationMisuse.cpp             ee7d34d6a424e5144a91847ee4abe886697605a0
src/thermal/CMakeLists.txt          + ThermalValidation.cpp
tests/CMakeLists.txt                + the new test sources
tests/compile_fail/CMakeLists.txt   + group thermalvalidation (33 cases)
tests/architecture/CMakeLists.txt   + 8 tests
tests/cli/ZeroMatchGuard.cmake      + 19 filters
tests/architecture/CheckValidationOwnership.cmake and tests/architecture/fixtures/validation-* (7 trees)
include/bettercad/thermal/ThermalValidation.hpp
```

Documents, scripts and the ADR are outside the eight paths and do not change the fingerprint.

## Whitespace audit of every changed and new file

No byte-order mark, no carriage return (the `.cmd` scripts excepted), no trailing whitespace, a final newline, and `git diff --check` clean.

## Qualification of this exact candidate

```text
fingerprint at the freeze (23:21)                 == the eight hashes above
fingerprint before the first build (23:28:21)     == the eight hashes above
fingerprint after the last test (04:41:04)        == the eight hashes above
result: 17 stages, 0 failed, first run on the frozen tree, 5h12m47s
```

`qualification-times.txt` records the three fingerprints; `postrun-summary.txt` records the build, test and cross-preset results; the numbers are in README.md. A first launch at 23:22
exited in under a second because `git` and `cmake` were not on the launching shell's PATH (exit 9009, no build, no test); its logs were deleted and the run was started again at 23:28
from the same frozen tree. Nothing was edited in between.