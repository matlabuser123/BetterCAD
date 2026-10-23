# P18-VIZ-001 — the freeze

```text
CANDIDATE:   the working tree on top of HEAD c56aef0521582b2eb20e7fc4b5cf52edeb618c17 (P18-VALID-001 record, origin == HEAD)
HEAD TREE:   4b444fed0d4b779f16e9336ca287a9996656f1c6
FROZEN:      2026-10-23 13:28, after the last change to a source, a test, a CMake file or a build rule
RULE:        any change to a source, a test, a contour or a legend rule, the cardinality policy, the unit boundary, the currentness logic or a build rule after this point invalidates
             the evidence and requires a new freeze and a new run
```

## The eight-path fingerprint

Computed from a scratch index (`read-tree HEAD`, `add -A` of the eight paths, `write-tree`), exactly as `qualify.cmd` computes it before the first build and after the last test:

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175
include            a7494226fef0afe51906c0c4df6f5f88ea29af48
src                54c8a486562028adc4d3e4a912a1d015a25c1400
tests              82afaa3ad0920b85ec3419ae3a58670615f4852b
examples           a955cf6511ce715fcb95bc06ef82b877535869e6
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
scratch tree  5406c70818f33cfca37c98780a48af39dfee584f
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are **equal to HEAD**: the application, the examples and the build rules were not touched.

## The fingerprints this milestone adds

```text
P18_VIZ_CONTRACT_FINGERPRINT         57207696d47fe68d69d6265dfd53bfd74a84a274
P18_VIZ_FIELD_MAPPING_FINGERPRINT    59448c7d8e766ce5d14f1c646f6a61d7f6fb991b
P18_VIZ_UNIT_BOUNDARY_FINGERPRINT    09f87e098cc2e112f5faedbe4092e30f24fe140a
P18_VIZ_CLASSIFIER_POLICY_FINGERPRINT abc169841908b1fee3a660c32ccea761f3346257
THERMAL_VIZ_OWNERSHIP_FINGERPRINT    b45e9acba59562775b83c479fb26594d809676be
ADR_050_DEPENDENCY_FINGERPRINT       1b01801c77674ac27c54869376286e5a6e8e9887
```

* **Contract**: the four public headers (`ThermalResultView.hpp`, `ThermalContour.hpp`, `ThermalInspection.hpp`, `ThermalOverlay.hpp`) and `Viewer.hpp`, which carries the one additive overload.
* **Field mapping**: `descriptorOf`, `gradientDisplayNorm`, both `channelValue` overloads and `classifyThermalState` — which stored member each channel reads, its location and unit, and how a scalar is taken
  (7680 bytes of source).
* **Unit boundary**: the OCCT overlay presentation (header and source), the OCCT mesh presentation, the offscreen test that measures the boundary against the CAD solid, the field-mapping digest, and the
  count of `toModelLength` uses (13) in the overlay presentation.
* **Classifier policy**: `classifyBoundaryCardinality` and `BoundaryClassStatus` (1936 bytes).
* **Ownership**: `CheckThermalVizOwnership.cmake` and its eleven fixture trees. **Dependency**: the renderer and thermal CMake lists and the two layering checkers (ADR-050).

The contract fingerprint differs from the one taken during development because the cache key gained its view fields and `ThermalOverlay` gained `incompleteTargets` after the independent review; this
is the final value.

## What was NOT changed

```text
untouched files: 265 equal 265 different 0
```

Every file of `src/thermal` and `include/bettercad/thermal`, `src/meshing` and `include/bettercad/meshing`, `src/structural` and `include/bettercad/structural`, `src/linalg` and `include/bettercad/linalg`,
`src/core` and `include/bettercad/core`, the P17 renderer files (`Structural*`, `MeshView`, `MeshHighlight`, `GeometryMeshMap`, `MeshContour`) and the OCCT mesh presentation, compared by blob against `HEAD`.
The list is in `qualification/freeze-viz.txt`.

## What changed

```text
blob src/renderer/ThermalResultView.cpp          0e655b1d1538303695a0e4b003776e15f6139413
blob src/renderer/ThermalContour.cpp             dde4e250bf848690a8f320ea1cae0c083d816078
blob src/renderer/ThermalInspection.cpp          8117289ff2a322288c49656bfdf84e4d03935818
blob src/renderer/ThermalOverlay.cpp             c470c2eb2c9367fa2059193f8b2c83619b4ebd72
blob tests/renderer/ThermalVizFieldTests.cpp     780c503a7bb95bb59c33cccd7d83e7877a2306c8
blob tests/renderer/ThermalVizContourTests.cpp   6653ef153841be9e9c7d5664383fb606e862b3b6
blob tests/renderer/ThermalVizStateTests.cpp     3d3591d348fcc04e75a8c9781ff8257c5ef87b7e
blob tests/renderer/ThermalVizBoundaryTests.cpp  485948d35695ff27b95b3b39131ab98493660976
blob tests/renderer/ThermalVizViewerTests.cpp    a31f0fba877dab7ef5ba6475663ee1749552d932
blob tests/renderer/ThermalVizTestSupport.hpp    935b1fa0d37c1e25e2d1b79836d3041e5d64aa73
blob tests/compile_fail/ThermalVizMisuse.cpp     8cd31ab5d2c1bd461135ad05cb9fbd457ba17042
include/bettercad/renderer/Thermal{ResultView,Contour,Inspection,Overlay}.hpp   (new)
include/bettercad/renderer/Viewer.hpp                + displayOverlay(ThermalOverlay), PresentationKind::ThermalOverlay
src/renderer/occt/OcctViewer.cpp, OcctOverlayPresentation.hpp/.cpp   + the thermal overlay presentation (one toModelLength)
src/renderer/CMakeLists.txt          + 4 sources, + BetterCAD::thermal
tests/CMakeLists.txt                 + the new test sources
tests/compile_fail/CMakeLists.txt    + group thermalviz (51 cases)
tests/architecture/CMakeLists.txt    + 12 tests
tests/cli/ZeroMatchGuard.cmake       + 15 filters
tests/architecture/CheckThermalVizOwnership.cmake and tests/architecture/fixtures/thermalviz-* (11 trees)
```

The four production sources have the same blobs as the ones the mutation campaign restored (`qualification/mutations/viz-premutation-hashes-pass2.txt`).

Documents, scripts, the ADR, `TODO.md` and `ROADMAP.md` are outside the eight paths and do not change the fingerprint.
